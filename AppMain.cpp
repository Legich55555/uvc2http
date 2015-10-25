/*******************************************************************************
#                                                                              #
# This file is part of uvc2http.                                           #
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
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include "Tracer.h"
#include "Config.h"
#include "StreamFunc.h"

namespace {

  volatile bool IsSigIntRaisedFlag = false;

  void sigIntHandler(int sig) {
    IsSigIntRaisedFlag = true;
  }
  
  bool IsSigIntRaised(void) {
    return IsSigIntRaisedFlag;
  }
  
  // This function disables auto focus and sets focus distance to ~1.2 meter (good choice for RC toys cameras). 
  bool SetupCamera(int cameraFd) {
    {
      // Disable auto focus
      
      v4l2_ext_controls ext_ctrls = {0};
      v4l2_ext_control ext_ctrl = {0};
      ext_ctrl.id = 10094860;
      ext_ctrl.value64 = 0;

      ext_ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
      ext_ctrls.count = 1;
      ext_ctrls.controls = &ext_ctrl;
      int ioctlResult = ioctl(cameraFd, VIDIOC_S_EXT_CTRLS, &ext_ctrls);
      if (ioctlResult != 0) {
        return false;
      }
    }

    { 
      // Set focus range ~1.2 meter
      
      const int focusValue = 80;
    
      v4l2_ext_controls ext_ctrls = {0};
      v4l2_ext_control ext_ctrl = {0};
      ext_ctrl.id = 10094858;
      ext_ctrl.value64 = focusValue;

      ext_ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
      ext_ctrls.count = 1;
      ext_ctrls.controls = &ext_ctrl;
      int ioctlResult = ioctl(cameraFd, VIDIOC_S_EXT_CTRLS, &ext_ctrls);
      if (ioctlResult != 0) {
        return false;
      }
    }

    return true;
  }
}

int main(int argc, char **argv) {
  
  UvcStreamerCfg config = GetConfig(argc, argv);
  if (!config.IsValid) {
    PrintUsage();
    return -1;
  }
  
  config.GrabberCfg.SetupCamera = SetupCamera;
  
  // Ignore SIGPIPE (OS sends it in case of transmitting to a closed TCP socket)
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    Tracer::Log("Failed to setup SIGPIPE handler.\n");
  }
  
  // Register handler for CTRL+C
  if (signal(SIGINT, sigIntHandler) == SIG_ERR) {
    Tracer::Log("Failed to setup SIGINT handler.\n");
  }
    
  // Register handler for termination
  if (signal(SIGTERM, sigIntHandler) == SIG_ERR) {
    Tracer::Log("Failed to setup SIGTERM handler.\n");
  }
  
  Tracer::Log("Starting streaming...");
  int res = UvcStreamer::StreamFunc(config, IsSigIntRaised);
  Tracer::Log("Streaming stopped with code %d", res);
    
  return res;
}
