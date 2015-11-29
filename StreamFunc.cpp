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
  
  int StreamFunc(const UvcStreamerCfg& config, ShouldExit shouldExit) {
    
    using namespace std::chrono;

    HttpServer httpServer;
    if (!httpServer.Init(config.ServerCfg.ServicePort.c_str())) {
      Tracer::Log("Failed to initialize HTTP server.\n");
      return -2;
    }
    
    // Init and configure UVC video camera
    UvcGrabber uvcGrabber(config.GrabberCfg);
    if (!uvcGrabber.Init()) {
      Tracer::Log("Failed to initialize UvcGrabber (is there a UVC camera?). The app will try to initialize it later.\n");
    }
    
    static const uint32_t measureFrames = 500U;
    
    unsigned queuedCountToHttpServer = 0U;
    
    while (!shouldExit()) {
      
      // Frames counters
      uint32_t frames = 0U;
      uint32_t eagains = 0U;
      uint32_t missed = 0U;
      uint32_t starvations = 0U;
      
      uint32_t stopFrameNumber = ~0U;
      uint32_t currentFrameNumber = 0U;
      
      microseconds startTs = duration_cast<microseconds>(system_clock::now().time_since_epoch());
      
      bool errorDetected = false;

      while (!shouldExit() && (currentFrameNumber < stopFrameNumber) ) {
        if (uvcGrabber.IsCameraReady() && !uvcGrabber.IsBroken()) {
          const VideoFrame* videoBuffer = uvcGrabber.DequeuFrame();
          
          if (videoBuffer != nullptr) {
            if (0 == frames) {
              stopFrameNumber = videoBuffer->V4l2Buffer.sequence + measureFrames;
            }
            else {
              missed += videoBuffer->V4l2Buffer.sequence - (currentFrameNumber + 1);
            }

            currentFrameNumber = videoBuffer->V4l2Buffer.sequence;

            if (!httpServer.QueueFrame(videoBuffer)) {
              uvcGrabber.RequeueFrame(videoBuffer);
            }
            else {
              queuedCountToHttpServer++;
            }
            
            frames++;
          }
          else {
            const timespec SLEEP_TIMESPEC {1, 0};
            ::nanosleep(&SLEEP_TIMESPEC, nullptr);

            eagains++;
          }
          
          if (queuedCountToHttpServer > 2) {
            bool forceHttpDequeue = (uvcGrabber.GetQueuedFramesNumber() <= 1);
            if (forceHttpDequeue) {
              starvations++;
            }

            const VideoFrame* releasedBuffer;
            
            do {
              releasedBuffer = httpServer.DequeueFrame(forceHttpDequeue);
              if (releasedBuffer != nullptr) {
                queuedCountToHttpServer--;
                uvcGrabber.RequeueFrame(releasedBuffer);
              }
            } while (queuedCountToHttpServer > 1 && releasedBuffer != nullptr);
          }
        }
        else {
          std::vector<const VideoFrame*> buffers = httpServer.DequeueAllFrames();
          for (auto buffer : buffers) {
            uvcGrabber.RequeueFrame(buffer);
          }
          
          // Repeat recovery attempts every second.
          const timespec RecoveryDelay {1, 0};
          ::nanosleep(&RecoveryDelay, nullptr);
          
          uvcGrabber.ReInit();
          
          errorDetected = true;
        }
        
        httpServer.ServeRequests();
      }
      
      if (!errorDetected) {
        microseconds stopTs = duration_cast<microseconds>(system_clock::now().time_since_epoch());
        microseconds duration = stopTs - startTs;
        
        float fps = (frames * 1000000.f) / duration.count();

        Tracer::Log("captured frames: %" PRIu32 ", "
                    "eagain count: %" PRIu32 ", "
                    "missed frames: %" PRIu32 ", "
                    "starvations: %" PRIu32 ", "
                    "fps: %f, "
                    "duration: %" PRIu64 ".\n",
                    frames, eagains, missed, starvations, fps, duration.count());
      }
    }
    
    httpServer.Shutdown();
    
    return 0;
  }
}
