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

#include "Config.h"

UvcStreamerCfg GetConfig(int argc, char **argv) {
  UvcStreamerCfg config;
  
  config.GrabberCfg.CameraDeviceName = "/dev/video0";
  config.GrabberCfg.FrameWidth = 1280U;
  config.GrabberCfg.FrameHeight = 720U;
  config.GrabberCfg.FrameRate = 30U;
  config.GrabberCfg.BuffersNumber = 4U;
  config.GrabberCfg.SetupCamera = nullptr;
  
  config.ServerCfg.ServicePort = "8081";
  
  config.IsValid = true;;

  return config;
}

void PrintUsage() {
}

