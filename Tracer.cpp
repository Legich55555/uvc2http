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

#include "Tracer.h"

#include <cstdio>
#include <cstdarg> 
#include <cstring>
#include <syslog.h>
#include <errno.h>

namespace {
  bool IsStdErrReady() {
    return fileno(stderr) != -1;
  }
}

namespace Tracer {
  static const char* TraceName = "UvcStreamer"; 
  
  void Log(const char* format, ...) {
    
    va_list args;
    va_start(args, format);

    if (IsStdErrReady()) {
      std::vfprintf(stderr, format, args);
    }
    else {
      static bool isSysLogInitialized = false;
      if (!isSysLogInitialized) {
        ::openlog(TraceName, LOG_ODELAY, LOG_USER | LOG_ERR);
        isSysLogInitialized = true;
      }

      ::vsyslog(LOG_ERR, format, args);
    }

    va_end(args);
  }
  
  void LogErrNo(const char* format, ...) {
    va_list args;
    va_start(args, format);
    Log(format, args);
    va_end(args);

    const char* errnoDescription = strerror(errno);
    Log("%s\n", errnoDescription);
  }
}
