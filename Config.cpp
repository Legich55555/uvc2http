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
#include <getopt.h>
#include <cstdlib>
#include "Tracer.h"

namespace {
  const uint32_t InvalidUInt32OptValue = 0xFFFFFFFF;
  
  uint32_t GetUInt32OptValue(char* arg) {
    static const int ConversionBase = 10;
    char* rest;
    
    int result = strtol(arg, &rest, ConversionBase);
    
    if (0 != *rest || result <= 0) {
      return InvalidUInt32OptValue;
    }
    
    return static_cast<uint32_t>(result);
  }
}

UvcStreamerCfg GetConfig(int argc, char **argv) {
  UvcStreamerCfg config;
  
  config.GrabberCfg.CameraDeviceName = "/dev/video0";
  config.GrabberCfg.FrameWidth = 640U;
  config.GrabberCfg.FrameHeight = 480U;
  config.GrabberCfg.FrameRate = 15U;
  config.GrabberCfg.BuffersNumber = 4U;
  config.GrabberCfg.SetupCamera = nullptr;
  
  config.ServerCfg.ServicePort = "8081";
  
  static option options[] = {
    {"d", required_argument, 0, 0}, // Camera device name
    {"device", required_argument, 0, 0}, // Camera device name
    {"b", required_argument, 0, 0}, // Capture buffers number
    {"buffers", required_argument, 0, 0}, // Capture buffers number
    {"w", required_argument, 0, 0}, // Width
    {"width", required_argument, 0, 0}, // Width
    {"h", required_argument, 0, 0}, // Height
    {"height", required_argument, 0, 0}, // Height
    {"f", required_argument, 0, 0}, // Frame rate
    {"fps", required_argument, 0, 0}, // Frame rate
    {"p", required_argument, 0, 0}, // TCP port
    {"port", required_argument, 0, 0}, // TCP port
    {0, 0, 0, 0}
  };
  
  bool foundError = false;
  
  while (!foundError) {
    int optionIdx;
    int optionCharacter = getopt_long_only(argc, argv, "", options, &optionIdx);

    // There are no more options to parse.
    if (-1 == optionCharacter)
    {
      break;
    }
    

    // Unexpected option
    if (optionCharacter != '?') {
      switch (optionIdx) {
          // d, device
          case 0:
          case 1:
            config.GrabberCfg.CameraDeviceName = optarg;
            break;

          // b, buffers
          case 2:
          case 3:
            {
              const uint32_t optVal = GetUInt32OptValue(optarg);
              if (optVal != InvalidUInt32OptValue) {
                config.GrabberCfg.BuffersNumber = optVal;
              }
              else {
                Tracer::Log("Invalid value '%s' for buffers number.\n", optarg);
                foundError = true;
              }
            }

            break;

          // w, width
          case 4:
          case 5:
            {
              const uint32_t optVal = GetUInt32OptValue(optarg);
              if (optVal != InvalidUInt32OptValue) {
                config.GrabberCfg.FrameWidth = optVal;
              }
              else {
                Tracer::Log("Invalid value '%s' for frame width.\n", optarg);
                foundError = true;
              }
            } 
            
            break;
          
          // h, height
          case 6:
          case 7:
            {
              const uint32_t optVal = GetUInt32OptValue(optarg);
              if (optVal != InvalidUInt32OptValue) {
                config.GrabberCfg.FrameHeight = optVal;
              }
              else {
                Tracer::Log("Invalid value '%s' for frame height.\n", optarg);
                foundError = true;
              }
            }
            
            break;

          // f, fps
          case 8:
          case 9:
            {
              const uint32_t optVal = GetUInt32OptValue(optarg);
              if (optVal != InvalidUInt32OptValue) {
                config.GrabberCfg.FrameRate = optVal;
              }
              else {
                Tracer::Log("Invalid value '%s' for frame rate.\n", optarg);
                foundError = true;
              }
            }
            
            break;

          case 10:
          case 11:
            {
              const uint32_t optVal = GetUInt32OptValue(optarg);
              if (optVal != InvalidUInt32OptValue) {
                config.ServerCfg.ServicePort = optarg;
              }
              else {
                Tracer::Log("Invalid value '%s' for port.\n", optarg);
                foundError = true;
              }
            }
            
            break;

          default:
            foundError = true;
      }
    }
    else {
      Tracer::Log("Unexpected parameter '%c'\n", optionCharacter);
      foundError = true;
    }
  }
  
  config.IsValid = !foundError;

  return config;
}

void PrintUsage() {
  printf("Usage: uvc_streamer -d /dev/video0 -b 4 -w 640 -h 480 -f 30 -p 8080\n");
}

