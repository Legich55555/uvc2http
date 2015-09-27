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
}

int main(int argc, char **argv) {
  
  const UvcStreamerCfg config = GetConfig(argc, argv);
  if (!config.IsValid) {
    PrintUsage();
    return -1;
  }
  
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
  
  Tracer::Log("Starting streaming... %s ", "AAAss");
  int res = UvcStreamer::StreamFunc(config, IsSigIntRaised);
  Tracer::Log("Streaming stopped with code %d", res);
    
  return res;
}
