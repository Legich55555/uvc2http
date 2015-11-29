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

#ifndef BUFFER_H
#define BUFFER_H

#include <cstdint>
#include <linux/types.h>
#include <linux/videodev2.h>

struct Buffer {
  const uint8_t* Data;
  uint32_t Size;
};

struct VideoFrame {
  const uint8_t* Data;  // Mmapped address of a v4l2 buffer.
  uint32_t Size;        // Current size of data in buffer.
  uint32_t Length;      // Buffer length (used for unmapping).
  uint32_t Idx;
  v4l2_buffer V4l2Buffer;
};
  
#endif