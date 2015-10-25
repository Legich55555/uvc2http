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

#ifndef STREAMERFUNC_H
#define STREAMERFUNC_H

#include "Config.h"

namespace UvcStreamer {
  /// @brief Type for a function which should be call to check if streaming should be stopped.
  typedef bool (*ShouldExit)(void);

  /// @brief StreamFunc does all streaming tasks.
  int StreamFunc(const UvcStreamerCfg& config, ShouldExit shouldExit);
}

#endif // STREAMERFUNC_H
