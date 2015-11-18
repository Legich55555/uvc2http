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
    
//     MeasureStream(uvcGrabber, 130000000);
//     MeasureStream(uvcGrabber, 80000000);
//     MeasureStream(uvcGrabber, 50000000);
//     MeasureStream(uvcGrabber, 20000000);
//     MeasureStream(uvcGrabber, 10000000);
//     MeasureStream(uvcGrabber, 9000000);
//     MeasureStream(uvcGrabber, 7000000);
//     MeasureStream(uvcGrabber, 5000000);
//     MeasureStream(uvcGrabber, 4000000);
//     MeasureStream(uvcGrabber, 3000000);
//     MeasureStream(uvcGrabber, 2000000);
//     MeasureStream(uvcGrabber, 1500000);
//     MeasureStream(uvcGrabber, 1000000);
//     MeasureStream(uvcGrabber, 750000);
//     MeasureStream(uvcGrabber, 500000);
//     MeasureStream(uvcGrabber, 250000);
//     MeasureStream(uvcGrabber, 100000);

// SleepTimeMicroSec:75000, framesCount: 91, eagainCount: 19910, missedFramesCount: 14.
// SleepTimeMicroSec:100000, framesCount: 125, eagainCount: 19875, missedFramesCount: 37.
// SleepTimeMicroSec:125000, framesCount: 124, eagainCount: 19876, missedFramesCount: 51.
// SleepTimeMicroSec:250000, framesCount: 152, eagainCount: 19848, missedFramesCount: 51.
// SleepTimeMicroSec:500000, framesCount: 218, eagainCount: 19782, missedFramesCount: 88.
// SleepTimeMicroSec:750000, framesCount: 371, eagainCount: 19629, missedFramesCount: 141.
// SleepTimeMicroSec:1000000, framesCount: 504, eagainCount: 19496, missedFramesCount: 209.
// SleepTimeMicroSec:1500000, framesCount: 669, eagainCount: 19331, missedFramesCount: 260.
// SleepTimeMicroSec:2000000, framesCount: 1015, eagainCount: 18985, missedFramesCount: 357.
// SleepTimeMicroSec:2500000, framesCount: 1265, eagainCount: 18735, missedFramesCount: 501.
// SleepTimeMicroSec:3000000, framesCount: 1634, eagainCount: 18366, missedFramesCount: 604.
// SleepTimeMicroSec:3500000, framesCount: 2055, eagainCount: 17945, missedFramesCount: 682.
// SleepTimeMicroSec:4000000, framesCount: 2550, eagainCount: 17450, missedFramesCount: 746.
// SleepTimeMicroSec:4500000, framesCount: 2832, eagainCount: 17168, missedFramesCount: 838.
// SleepTimeMicroSec:5000000, framesCount: 3367, eagainCount: 16633, missedFramesCount: 878.
// SleepTimeMicroSec:50000, framesCount: 3710, eagainCount: 16290, missedFramesCount: 1006.
// SleepTimeMicroSec:75000, framesCount: 129, eagainCount: 19871, missedFramesCount: 27.
// SleepTimeMicroSec:100000, framesCount: 140, eagainCount: 19860, missedFramesCount: 36.
// SleepTimeMicroSec:125000, framesCount: 163, eagainCount: 19837, missedFramesCount: 41.
// SleepTimeMicroSec:250000, framesCount: 172, eagainCount: 19828, missedFramesCount: 51.
// SleepTimeMicroSec:500000, framesCount: 264, eagainCount: 19736, missedFramesCount: 74.


    const std::vector<long> sleepTimes = {
      0,
      10000,
      25000,
      50000,
      75000,
      100000,
      125000,
      250000,
      500000,
      750000,
      1000000,
      1500000,
      2000000,
      2500000,
      3000000,
      3500000,
      4000000,
      4500000,
      5000000,
      6000000,
      7000000,
      8000000,
      9000000,
      10000000,
      11000000,
      12000000,
      13000000,
      16000000,
      17000000,
      18000000,
      19000000
    };
    
    for (size_t i = 0; i < sleepTimes.size() && !shouldExit(); ++i) {

      const uint32_t measureFrames = 500U;
      const long sleepTimeMicroSec = sleepTimes[i];
      
      size_t framesCount = 0U;
      size_t eagainCount = 0U;
      size_t missedFramesCount = 0U;
    
      uint32_t stopFrameNumber = ~0U;
      uint32_t currentFrameNumber = 0U;
      
      using namespace std::chrono;

      microseconds startTs = duration_cast<microseconds>(system_clock::now().time_since_epoch());

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
        
          static const long MaxServeTimeMicroSec = (1000000 / 2) / config.GrabberCfg.FrameRate;
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
        }
      }
      
      microseconds stopTs = duration_cast<microseconds>(system_clock::now().time_since_epoch());
      microseconds duration = stopTs - startTs;
      
      float fps = (framesCount * 1000000.f) / duration.count();

      Tracer::Log("framesCount: %d, eagainCount: %d, missedFramesCount: %d, fps: %f, sleepTimeMicroSec: %lu.\n",
                  framesCount, eagainCount, missedFramesCount, fps, sleepTimeMicroSec);
    }
    
    return 0;
  }
}
