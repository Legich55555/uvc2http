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

#ifndef UVCGRABBER_H
#define UVCGRABBER_H

#include <string>
#include <vector>

struct VideoBuffer;


/*
 * @brief UvcGrabber captures data from UVC camera device.
 * 
 * */
class UvcGrabber
{
public:
  
  typedef bool(*SetupCameraFunc)(int);
  
  struct Config {
    std::string CameraDeviceName;
    uint32_t FrameWidth;
    uint32_t FrameHeight;
    uint32_t FrameRate; 
    uint32_t BuffersNumber;

    // Function which is called for app specific camera configuration.
    SetupCameraFunc SetupCamera;
  };

  explicit UvcGrabber(const Config& config);
  ~UvcGrabber();
  
  // @brief Initializes video capture. Returns true if initialization was successfull.
  bool Init();
  
  // @brief ReInitializes video capture. Returns true if initialization was successfull.
  bool ReInit();
  
  // @brief Shutdowns video capture and free all used resources.
  void Shutdown();

  // @brief Return true if fatal error happened during dequeuing/requeing of frames
  //        Expected recovery steps: requeu all dequed frames, call Shutdown() and then Init().
  bool IsBroken() const { return _isBroken; }

  // @brief Return true if camera was successfully initialized.
  bool IsCameraReady() const { return _cameraFd != -1; }

  const VideoBuffer* DequeuFrame();

  void RequeueFrame(const VideoBuffer* buffer);

  UvcGrabber() = delete;
  UvcGrabber(const UvcGrabber& other) = delete;
  UvcGrabber& operator=(const UvcGrabber& other) = delete;
  
private:
  
  Config _config;
  std::vector<VideoBuffer> _videoBuffers;
  int _cameraFd;
  bool _isBroken;
};

#endif // UVCGRABBER_H
