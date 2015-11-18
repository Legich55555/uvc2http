/*******************************************************************************
#                                                                              #
# This file is part of uvc2http.                                               #
#                                                                              #
# Copyright (C) 2015 Oleg Efremov                                              #
#                                                                              #
# Uvc_streamer is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; version 2 of the License.                      #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
*******************************************************************************/

#include "StreamFunc.h"

#include <cstdio>
#include <cinttypes>
#include <time.h>
#include <chrono>
#include <vector>

#include "Tracer.h"
#include "UvcGrabber.h"
#include "HttpServer.h"
#include "MjpegUtils.h"

namespace UvcStreamer {
  
  void MeasureStream(UvcGrabber& uvcGrabber, unsigned sleepInNanoSeconds) {

    size_t eagainCount = 0;
    size_t frameCount = 0;
    uint32_t lastSeq = 0;
    
    using namespace std::chrono;
    milliseconds ms1 = duration_cast< milliseconds >(system_clock::now().time_since_epoch());
    
    while (frameCount < 1000U) {
      
      if (uvcGrabber.IsCameraReady() && !uvcGrabber.IsBroken()) {
        const VideoBuffer* videoBuffer = uvcGrabber.DequeuFrame();
        if (videoBuffer != nullptr) {
          if (0 == lastSeq) {
            lastSeq = videoBuffer->V4l2Buffer.sequence;
          }
          else {
            if (lastSeq < videoBuffer->V4l2Buffer.sequence) {
              frameCount++;
              lastSeq = videoBuffer->V4l2Buffer.sequence;
            }
          }

          uvcGrabber.RequeueFrame(videoBuffer);
        }
        else {
          eagainCount++;
        }
      
        const timespec SleepTime {.tv_sec = 0, .tv_nsec = sleepInNanoSeconds};
        ::nanosleep(&SleepTime, nullptr);
      }
      else {
        // Repeat recovery attempts every second.
        const timespec RecoveryDelay {1, 0};
        ::nanosleep(&RecoveryDelay, nullptr);
        
        uvcGrabber.ReInit();
      }
    }
    
    milliseconds ms2 = duration_cast< milliseconds >(system_clock::now().time_since_epoch());
    milliseconds d = ms2 - ms1;
    Tracer::Log("sleepInNanoSeconds: %d, d: %llu, fps: %f, eagain: %d.\n",
                sleepInNanoSeconds, d.count(), (1000.f * 1000) / d.count(), eagainCount);
//    std::cout << sleepInNanoSeconds << ", d: " << d.count() << ", fps:" << 1000.f / d.count() * 1000 << " eagain: " << eagainCount << std::endl;
  }
  
  
  int StreamFunc(const UvcStreamerCfg& config, ShouldExit shouldExit) {
    
    HttpServer httpServer;
    if (!httpServer.Init(config.ServerCfg.ServicePort.c_str())) {
      Tracer::Log("Failed to initialize HTTP server.\n");
      return -2;
    }
    
    // Init and configure UVC video camera
    UvcGrabber uvcGrabber(config.GrabberCfg);
    if (!uvcGrabber.Init()) {
      Tracer::Log("Failed to initialize UvcGrabber (is there a UVC camera?). The app will try to initialize later.\n");
    }
    
    while (!shouldExit()) {

      const uint32_t measureFrames = 500U;
      const long sleepTimeMicroSec = 5000000;
      
      uint32_t framesCount = 0U;
      uint32_t eagainCount = 0U;
      uint32_t missedFramesCount = 0U;
      uint32_t stopFrameNumber = ~0U;
      uint32_t currentFrameNumber = 0U;
      
      using namespace std::chrono;

      microseconds startTs = duration_cast<microseconds>(system_clock::now().time_since_epoch());
      
      bool errorDetected = false;

      while (!shouldExit() && (currentFrameNumber < stopFrameNumber) ) {
        if (uvcGrabber.IsCameraReady() && !uvcGrabber.IsBroken()) {
          const VideoBuffer* videoBuffer = uvcGrabber.DequeuFrame();
          if (videoBuffer != nullptr) {
            if (0 == framesCount) {
              stopFrameNumber = videoBuffer->V4l2Buffer.sequence + measureFrames;
            }
            else {
              missedFramesCount += videoBuffer->V4l2Buffer.sequence - (currentFrameNumber + 1);
            }

            currentFrameNumber = videoBuffer->V4l2Buffer.sequence;

            if (!httpServer.QueueBuffer(videoBuffer)) {
              uvcGrabber.RequeueFrame(videoBuffer);
            }
            
            framesCount++;
          }
          else {
            eagainCount++;
          }
        
          static const long MaxServeTimeMicroSec = 0; //(1000000 / 2) / config.GrabberCfg.FrameRate;
          httpServer.ServeRequests(MaxServeTimeMicroSec);
          
          // It is safe to sleep for 0.5 ms. There is no significant 
          // difference (in terms of CPU utilization) in comparison with 
          // sleeping for a rest of time.
          
          const timespec sleepTimeSpec {.tv_sec = 0, .tv_nsec = sleepTimeMicroSec};
          ::nanosleep(&sleepTimeSpec, nullptr);
          
          bool forceHttpDequeue = (uvcGrabber.GetQueuedFramesNumber() <= 2); 

          const VideoBuffer* releasedBuffer = httpServer.DequeueBuffer(forceHttpDequeue);
          while (releasedBuffer != nullptr) {
            uvcGrabber.RequeueFrame(releasedBuffer);
          
            releasedBuffer = httpServer.DequeueBuffer(false);
          }
        }
        else {
          std::vector<const VideoBuffer*> buffers = httpServer.DequeueAllBuffers();
          for (auto buffer : buffers) {
            uvcGrabber.RequeueFrame(buffer);
          }
          
          // Repeat recovery attempts every second.
          const timespec RecoveryDelay {1, 0};
          ::nanosleep(&RecoveryDelay, nullptr);
          
          uvcGrabber.ReInit();
          
          errorDetected = true;
        }
      }
      
      if (!errorDetected) {
        microseconds stopTs = duration_cast<microseconds>(system_clock::now().time_since_epoch());
        microseconds duration = stopTs - startTs;
        
        float fps = (framesCount * 1000000.f) / duration.count();

        PRId64;
        
        Tracer::Log("framesCount: %" PRIu32 ", "
                    "eagainCount: %" PRIu32 ", "
                    "missedFramesCount: %" PRIu32 ", "
                    "fps: %f, "
                    "sleepTimeMicroSec: %ld, "
                    "duration: %" PRIu64 ".\n",
                    framesCount, eagainCount, missedFramesCount, fps, sleepTimeMicroSec, duration.count());
      }
    }
    
    return 0;
  }
}
