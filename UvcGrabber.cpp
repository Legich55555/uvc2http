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

#include "UvcGrabber.h"

#include <new>
#include <time.h>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <vector>

#include "Tracer.h"
#include "Buffer.h"

static const uint32_t IoctlMaxTries = 5U;

namespace 
{
  /// @brief Executes ioctl and if it fails then try to repeat.
  ///        Exceptions: EINTR, EAGAIN and ETIMEDOUT
  int Ioctl(int fd, int request, unsigned triesNumber, void *arg);
}

UvcGrabber::UvcGrabber(const UvcGrabber::Config& config)
  : _config(config),
    _cameraFd(-1),
    _isBroken(false),
    _queuedBuffersCount(0)
{
}

UvcGrabber::~UvcGrabber()
{
  Shutdown();
}

bool UvcGrabber::Init()
{
  if (_isBroken) {
    return false;
  }
  
  _cameraFd = open(_config.CameraDeviceName.c_str(), O_RDWR | O_NONBLOCK);
  if (-1 == _cameraFd) {
    Tracer::LogErrNo("Failed open().\n");
    _cameraFd = -1;
    _isBroken = true;
    return false;
  }
  
  if (!SetupCamera()) {
    ::close(_cameraFd);
    _cameraFd = -1;
    _isBroken = true;
    return false;
  }
  
  if (!SetupBuffers()) {
    Tracer::Log("Failed SetupBuffers().\n");
    ::close(_cameraFd);
    _cameraFd = -1;
    _isBroken = true;
    return false;
  }
  
  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  int ioctlResult = Ioctl(_cameraFd, VIDIOC_STREAMON, IoctlMaxTries, &type);
  if (ioctlResult != 0) {
    Tracer::Log("Failed Ioctl(VIDIOC_STREAMON).\n");
    FreeBuffers();
    ::close(_cameraFd);
    _cameraFd = -1;
    _isBroken = true;
    return false;
  }

  return true;
}
    
bool UvcGrabber::ReInit()
{
  Shutdown();
  
  return Init();
}
    
void UvcGrabber::Shutdown()
{
  if (_cameraFd != -1) {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ioctlResult = Ioctl(_cameraFd, VIDIOC_STREAMOFF, IoctlMaxTries, &type);
    if (ioctlResult != 0) {
      Tracer::Log("Failed Ioctl(VIDIOC_STREAMOFF).\n");
    }

    FreeBuffers();
    ::close(_cameraFd);

    _cameraFd = -1;
  }
  
  _isBroken = false;
  _queuedBuffersCount = 0;
}

const VideoBuffer* UvcGrabber::DequeuFrame()
{
  if (_isBroken) {
    Tracer::Log("Invalid call for DequeuFrame.\n");
    return nullptr;
  }
  
  v4l2_buffer v4l2Buffer = {0};
  v4l2Buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  v4l2Buffer.memory = V4L2_MEMORY_MMAP;
  
  bool dequeueResult = DequeuBuffer(v4l2Buffer);
  
  if (_isBroken) {
    Tracer::Log("Failed Ioctl(VIDIOC_DQBUF) %d.\n", errno);
    return nullptr;
  }
  
  if (!dequeueResult) {
    return nullptr;
  }
  
  if (v4l2Buffer.index >= _videoBuffers.size()) {
    Tracer::Log("Unexpected buffer index.\n");

    QueueBuffer(v4l2Buffer);
    _isBroken = true;

    return nullptr;
  }
  
  _videoBuffers[v4l2Buffer.index].Size = v4l2Buffer.bytesused;
  _videoBuffers[v4l2Buffer.index].V4l2Buffer = v4l2Buffer;
  
  return &(_videoBuffers[v4l2Buffer.index]);
}

void UvcGrabber::RequeueFrame(const VideoBuffer* videoBuffer) 
{
  if (videoBuffer->V4l2Buffer.index >= _videoBuffers.size()) {
    Tracer::Log("Unexpected buffer index.\n");
    return;
  }
  
  QueueBuffer(_videoBuffers[videoBuffer->Idx].V4l2Buffer);
}

void UvcGrabber::QueueBuffer(v4l2_buffer& v4l2Buffer)
{
  int ioctlResult = Ioctl(_cameraFd, VIDIOC_QBUF, IoctlMaxTries, &v4l2Buffer);
  if (ioctlResult < 0) {
    Tracer::Log("Failed Ioctl(VIDIOC_QBUF).\n");
    _isBroken = true;
  }
  
  _queuedBuffersCount += 1U;
}

bool UvcGrabber::DequeuBuffer(v4l2_buffer& v4l2Buffer)
{
  int ioctlResult = Ioctl(_cameraFd, VIDIOC_DQBUF, IoctlMaxTries, &v4l2Buffer);
  if (ioctlResult != 0) {
    if (errno != EAGAIN && errno != EINTR && errno != ETIMEDOUT) {
      Tracer::Log("Failed Ioctl(VIDIOC_DQBUF) %d.\n", errno);
      _isBroken = true;
    }

    return false;
  }
  
  _queuedBuffersCount -= 1U;
  
  return true;
}

void UvcGrabber::FreeBuffers()
{
  // Unmap buffers
  for (size_t bufferIdx = 0; bufferIdx < _videoBuffers.size(); ++bufferIdx) {
    if (0 != munmap(const_cast<uint8_t*>(_videoBuffers[bufferIdx].Data), _videoBuffers[bufferIdx].Length)) {
      Tracer::Log("Failed munmap().\n");
    }
  }
  
  // Change buffers number to 0
  v4l2_requestbuffers requestBuffers = {0};
  requestBuffers.count = 0;
  requestBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  requestBuffers.memory = V4L2_MEMORY_MMAP;
  int ioctlResult = Ioctl(_cameraFd, VIDIOC_REQBUFS, IoctlMaxTries, &requestBuffers);
  if (ioctlResult < 0) {
    Tracer::Log("Failed Ioctl(VIDIOC_REQBUFS).\n");
  }
    
  _videoBuffers.resize(0);
}

bool UvcGrabber::SetupBuffers()
{
  _videoBuffers.reserve(_config.BuffersNumber);
  
  {
    v4l2_requestbuffers requestBuffers = {0};
    requestBuffers.count = _config.BuffersNumber;
    requestBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    requestBuffers.memory = V4L2_MEMORY_MMAP;
    int ioctlResult = Ioctl(_cameraFd, VIDIOC_REQBUFS, IoctlMaxTries, &requestBuffers);
    if (ioctlResult < 0) {
      Tracer::Log("Failed Ioctl(VIDIOC_REQBUFS).\n");
      return false;
    }
  }
  
  for (size_t bufferIdx = 0; bufferIdx < _config.BuffersNumber; ++bufferIdx) {
    v4l2_buffer buffer = {0};
    buffer.index = bufferIdx;
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    
    int ioctlResult = Ioctl(_cameraFd, VIDIOC_QUERYBUF, IoctlMaxTries, &buffer);
    if (ioctlResult != 0) {
      Tracer::Log("Failed Ioctl(VIDIOC_QUERYBUF).\n");
      break;
    }
    
    QueueBuffer(buffer);
    if (_isBroken) {
      break;
    }

    void* bufferAddress = mmap(0, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, _cameraFd, buffer.m.offset);
    if (bufferAddress == MAP_FAILED) {
      Tracer::Log("Failed mmap().\n");
      break;
    }
    
    _videoBuffers.push_back(VideoBuffer {0});
    
    _videoBuffers[bufferIdx].Data = static_cast<uint8_t*>(bufferAddress);
    _videoBuffers[bufferIdx].Idx = bufferIdx;
    _videoBuffers[bufferIdx].Length = buffer.length;
  }  
  
  // If number of buffers is less than reqiested than unmap and free buffers.
  if (_videoBuffers.size() != _config.BuffersNumber) {
    FreeBuffers();
    return false;
  }
  
  return true;
}

bool UvcGrabber::SetupCamera() 
{
  v4l2_capability cameraCaps = {0};
  int ioctlResult = Ioctl(_cameraFd, VIDIOC_QUERYCAP, IoctlMaxTries, &cameraCaps);
  if (ioctlResult < 0) {
    Tracer::Log("Ioctl(VIDIOC_QUERYCAP) failed.\n");
    return false;
  }

  if (!(cameraCaps.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    Tracer::Log("Error: device does not support video capture.\n");
    return false;
  }

  if (!(cameraCaps.capabilities & V4L2_CAP_STREAMING)) {
    Tracer::Log("Error: device does not support streaming\n");
    return false;
  }

  v4l2_format streamFormat = {0};
  streamFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  streamFormat.fmt.pix.width = _config.FrameWidth;
  streamFormat.fmt.pix.height = _config.FrameHeight;  
  streamFormat.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;  
  streamFormat.fmt.pix.field = V4L2_FIELD_ANY;  
  ioctlResult = Ioctl(_cameraFd, VIDIOC_S_FMT, IoctlMaxTries, &streamFormat);
  if (ioctlResult < 0) {
    Tracer::Log("Failed Ioctl(VIDIOC_S_FMT + V4L2_BUF_TYPE_VIDEO_CAPTURE).\n");
    return false;
  }

  // TODO: Check if output format is same as input.
  {
    v4l2_streamparm streamParm = {0};
    streamParm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctlResult = Ioctl(_cameraFd, VIDIOC_G_PARM, IoctlMaxTries, &streamParm);
    if (ioctlResult < 0) {
      Tracer::Log("Failed Ioctl(VIDIOC_G_PARM + V4L2_BUF_TYPE_VIDEO_CAPTURE), error: %d\n", ioctlResult);
      return false;
    }
  }

  {
    v4l2_streamparm streamParm = {0};
    streamParm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamParm.parm.capture.timeperframe.numerator = 1;
    streamParm.parm.capture.timeperframe.denominator = _config.FrameRate;
    ioctlResult = Ioctl(_cameraFd, VIDIOC_S_PARM, IoctlMaxTries, &streamParm);
    if (ioctlResult < 0) {
      Tracer::Log("Failed Ioctl(VIDIOC_S_PARM + V4L2_BUF_TYPE_VIDEO_CAPTURE), error: %d\n", ioctlResult);
      return false;
    }
  }

  if (_config.SetupCamera != nullptr && !_config.SetupCamera(_cameraFd)) {
    Tracer::Log("Failed custom SetupCamera\n");
    return false;
  }
  
  return true;
}

namespace 
{
  /// @brief Executes ioctl and if it fails then try to repeat.
  ///        Exceptions: EINTR, EAGAIN and ETIMEDOUT
  int Ioctl(int fd, int request, unsigned triesNumber, void *arg)
  {
      int result = -1;
      for (unsigned i = 0; i < triesNumber; i++)
      {
        result = ioctl(fd, request, arg);
        if (0 == result || !(errno == EINTR || errno == EAGAIN || errno == ETIMEDOUT)) {
          break;
        }
      } 
      
      return result;
  }

}
