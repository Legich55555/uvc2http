/*******************************************************************************
#                                                                              #
# This file is part of uvc_streamer.                                           #
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

#include <cstdio>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "UvcGrabber.h"
#include "HttpServer.h"
#include "MjpegUtils.h"
#include "Config.h"

namespace {

  volatile bool ShouldExit = false;

  void sigIntHandler(int sig)
  {
    ShouldExit = true;
  }
}

int main(int argc, char **argv) {
  
  const UvcStreamerCfg config = GetConfig(argc, argv);
  if (!config.IsValid) {
    PrintUsage();
    return -1;
  }
  
  // Ignore SIGPIPE (OS sends it if transmitting to closed TCP sockets)
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    std::fprintf(stderr, "Failed to setup SIGPIPE handler.\n");
  }
  
  // Register handler for CTRL+C
  if (signal(SIGINT, sigIntHandler) == SIG_ERR) {
    std::fprintf(stderr, "Failed to setup SIGINT handler.\n");
  }
    
  // Register handler for termination
  if (signal(SIGTERM, sigIntHandler) == SIG_ERR) {
    std::fprintf(stderr, "Failed to setup SIGTERM handler.\n");
  }
    
  // Init and configure UVC video camera
  UvcGrabber uvcGrabber(config.GrabberCfg);
  if (!uvcGrabber.Init()) {
    std::fprintf(stderr, "Failed to initialize UvcGrabber (is there a UVC camera?). The app will try to initialize later.\n");
  }
  
  HttpServer httpServer;
  if (!httpServer.Init(config.ServerCfg.ServicePort.c_str())) {
    std::fprintf(stderr, "Failed to initialize HTTP server.\n");
    return -2;
  }
  
  const long Kilo = 1000;

  const long maxServeTimeMicroSec = (Kilo * Kilo) / config.GrabberCfg.FrameRate / 2;
  
  while (!ShouldExit) {
    if (uvcGrabber.IsCameraReady() && !uvcGrabber.IsBroken()) {
      const VideoBuffer* videoBuffer = uvcGrabber.DequeuFrame();
      if (videoBuffer != nullptr) {
          if (!httpServer.QueueBuffer(videoBuffer)) {
            uvcGrabber.RequeueFrame(videoBuffer);
          }
      }
    
      httpServer.ServeRequests(maxServeTimeMicroSec);
      
      // It is safe to sleep for 1 ms. There is no significant 
      // difference (in terms of CPU utilization) in comparison with 
      // sleeping for a rest of time.
      const timespec SleepTime {0, Kilo * Kilo};
      ::nanosleep(&SleepTime, nullptr);

      const VideoBuffer* releasedBuffer = httpServer.DequeueBuffer();
      while (releasedBuffer != nullptr) {
        uvcGrabber.RequeueFrame(releasedBuffer);
      
        releasedBuffer = httpServer.DequeueBuffer();
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
  
  return 0;
}
