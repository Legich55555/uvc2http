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

#include "HttpServer.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <functional>
#include <algorithm>
#include <chrono>

#include "MjpegUtils.h"
#include "Tracer.h"

namespace {

  const static size_t MaxServersNum = 8U;
  const static size_t MaxClientsNum = 20U;

  const size_t ClientReadBufferSize = 2048U;
  std::vector<uint8_t> ClientReadBuffer(ClientReadBufferSize);

  static const uint8_t HttpBoundaryValue[] = "\r\n--BoundaryDoNotCross\r\n";

  static const char HttpHeader[] = 
    "HTTP/1.0 200 OK\r\n" \
    "Connection: close\r\n" \
    "Server: uvc-streamer/0.01\r\n" \
    "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\r\n" \
    "Pragma: no-cache\r\n" \
    "Expires: Thu, 1 Jan 1970 00:00:01 GMT\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=BoundaryDoNotCross\r\n" \
    "\r\n" \
    "--BoundaryDoNotCross\r\n";

  static const uint32_t HeaderSize = sizeof(HttpHeader) - 1;

  bool SetupListeningSocket(int socketFd, const addrinfo* addrInfo, int maxPendingConnections);
  int CreateListeningSocket(const addrinfo* addrInfo);
}

HttpServer::HttpServer()
{
  _listeningFds.reserve(MaxServersNum);
}

HttpServer::~HttpServer()
{
}

bool HttpServer::Init(const char* servicePort)
{
  addrinfo addrHints = {0};
  addrHints.ai_family = PF_INET; // Only IPv4
  addrHints.ai_flags = AI_PASSIVE;
  addrHints.ai_socktype = SOCK_STREAM;
  
  int result = 0;
  
  addrinfo* addrInfoHead;
  if ((result = ::getaddrinfo(nullptr, servicePort, &addrHints, &addrInfoHead)) != 0) {
    Tracer::LogErrNo(::gai_strerror(result));
    return false;
  }
  
  addrinfo* currAddrInfo = addrInfoHead;
  while (currAddrInfo != nullptr) {
    int currAddrFd = CreateListeningSocket(currAddrInfo);
    if (currAddrFd != -1) {
      _listeningFds.push_back(currAddrFd);
    }
    else {
      Tracer::Log("Failed to create a listening socket.\n");
    }
    
    currAddrInfo = currAddrInfo->ai_next;
  }
  
  ::freeaddrinfo(addrInfoHead); 
  
  return !_listeningFds.empty();
}

void HttpServer::Shutdown()
{
  DequeueAllFrames();
  
  for (auto fd : _listeningFds) {
    if (-1 == ::close(fd)) {
      Tracer::LogErrNo("close().\n");
    }
  } 
  _listeningFds.clear();

  for (auto client : _beingServedClients) {
    if (-1 == ::close(client.first)) {
      Tracer::LogErrNo("close().\n");
    }
  }  
  _beingServedClients.clear();
  
  for (auto client : _waitingClients) {
    if (-1 == ::close(client.first)) {
      Tracer::LogErrNo("close().\n");
    }
  }  
  _waitingClients.clear();
}


void HttpServer::ServeRequests()
{
  if (HasDataToSend()) {
    SendData();
  }

  // Check for new connections.
  
  if (_listeningFds.empty()) {
    return;
  }
  
  size_t cientsNumber = GetClientsNumber();
  
  if (cientsNumber != 0) {
    // Count and accept only for every 100th call.
    static uint32_t listenCount = 0;
    listenCount += 1;
    if ((listenCount % 100) == 0) {
      return;
    }
  }
  
  fd_set selectfds;
  FD_ZERO(&selectfds);
  
  int maxFd = 0;
  
  for (size_t i = 0; i < _listeningFds.size(); ++i) {
    if (_listeningFds[i] != -1) {
      FD_SET(_listeningFds[i], &selectfds);
      
      if (_listeningFds[i] > maxFd) {
        maxFd = _listeningFds[i];
      }
    }
  }

  // If there are no clients then we can sleep for long time.
  const static timeval NO_WAIT_TIMEVAL = { 0 };
  const static timeval WAIT_TIMEVAL = { .tv_sec = 1, .tv_usec = 0 };
  timeval selectTimeval = (cientsNumber != 0) ? NO_WAIT_TIMEVAL : WAIT_TIMEVAL;
  
  int result = ::select(maxFd + 1, &selectfds, nullptr, nullptr, &selectTimeval);
  if (result < 0 && errno != EINTR) {
    Tracer::LogErrNo("select().\n");
    return;
  }
  
  for (size_t i = 0; i < _listeningFds.size(); ++i) {
    if (_listeningFds[i] != -1 && FD_ISSET(_listeningFds[i], &selectfds)) {
      
      sockaddr_storage sockAddr = {0};
      socklen_t sockAddrSize = sizeof(sockAddr);
      int clientFd = ::accept(_listeningFds[i], reinterpret_cast<sockaddr*>(&sockAddr), &sockAddrSize);
      if (-1 != clientFd) {
        if (GetClientsNumber() < MaxClientsNum) {
          
          // This value should be calculated from picture size, fps and network conditions.
          static const int SEND_BUFFER_SIZE = 512 * 1024;
          
          int sendBuffSetSize = SEND_BUFFER_SIZE;
          result = setsockopt(clientFd, SOL_SOCKET, SO_SNDBUF, &sendBuffSetSize, sizeof(sendBuffSetSize));
          if (-1 == result) {
            Tracer::LogErrNo("setsockopt() SOL_SOCKET, SO_SNDBUF, %d.\n", sendBuffSetSize);
          }
          
          socklen_t optLen;
          int sendBuffGetSize;
          result = getsockopt(clientFd, SOL_SOCKET, SO_SNDBUF, &sendBuffGetSize, &optLen);
          if (-1 == result) {
            Tracer::LogErrNo("getsockopt() SOL_SOCKET, SO_SNDBUF.");
          }
          else {
            Tracer::Log("Successfully changed socket send buffer. Requested size: %d, blessed size: %d.\n", 
                        sendBuffSetSize, sendBuffGetSize);
          }
          
          result = ::fcntl(clientFd, F_SETFL, O_NONBLOCK);
          if (-1 == result) {
            Tracer::LogErrNo("fcntl() F_SETFL, O_NONBLOCK.");
            ::close(clientFd);
          }
          else {
            _waitingClients[clientFd] = RequestInfo();
          }
        }
        else {
          Tracer::Log("Client dropped because of MaxClientsNum.\n");
          ::close(clientFd);
        }
      }
      else {
        Tracer::LogErrNo("accept().\n");
      }
    }
  }
  
  ReadAndParseRequests();
}

bool HttpServer::HasDataToSend() const
{
   for (const std::pair<int, ResponseInfo>& client : _beingServedClients) {
    
    const ResponseInfo& responseInfo = client.second;
    
    if (responseInfo.HeaderBytesSent < HeaderSize || responseInfo.VideoFrameIdx != ResponseInfo::InvalidBufferIdx) {
      return true;
    }
  }  
  
  for (const QueueItem& frame : _incomeQueue) {
    if (0 == frame.SentCounter) {
      return true;
    }
  }
  
  return false;
}

void HttpServer::ReadAndParseRequests()
{
  fd_set selectFds;
  FD_ZERO(&selectFds);
  
  int maxFd = 0;
  
  for (const std::pair<int, RequestInfo>& client : _waitingClients) {
    int clientFd = client.first;
    
    FD_SET(clientFd, &selectFds);
    
    if (clientFd > maxFd) {
      maxFd = clientFd;
    }
  }
  
  if (0 == maxFd) {
    return;
  }
  
  timeval selectTimeSpec = {0};
  int result = ::select(maxFd + 1, &selectFds, nullptr, nullptr, &selectTimeSpec);
  if (result < 0 && errno != EINTR && errno != EBADF) {
    Tracer::LogErrNo("select().\n");
    
    // Something wrong happened with waiting clients. Drop all of them.
    
    for (auto waitingClient : _waitingClients) {
      result = ::close(waitingClient.first);
      if (result < 0) {
        Tracer::LogErrNo("close().\n");
      }
    }
    
    _waitingClients.clear();
    return;
  }
  
  std::list<int> brokenClientFds;
  std::list<int> parsedClientFds;
  
  for (auto waitingClient : _waitingClients) {
    int clientFd = waitingClient.first;
    RequestInfo& requestInfo = waitingClient.second;
    
    if (FD_ISSET(clientFd, &selectFds)) {
      ssize_t readResult = ::read(clientFd, ClientReadBuffer.data(), ClientReadBuffer.size());
      if (readResult > 0) {
        requestInfo.RequestData.insert(requestInfo.RequestData.end(), ClientReadBuffer.data(), ClientReadBuffer.data() + readResult);
        
        for (auto ch : ClientReadBuffer) {
          if (ch == '\n') {
            parsedClientFds.push_back(clientFd);
            break;
          }
        }
      }
      else if (EAGAIN == readResult || EWOULDBLOCK == readResult) {
      }
      else {
        // Something wrong with a client. Close it.
        _waitingClients.erase(clientFd);
        brokenClientFds.push_back(clientFd);
      }
    }
  }
  
  for (auto brokenClientFd : brokenClientFds) {
    if (::close(brokenClientFd) != 0) {
      Tracer::LogErrNo("close().\n");
    }
  }
  
  for (auto parsedClientFd : parsedClientFds) {
    _waitingClients.erase(parsedClientFd);
    _beingServedClients[parsedClientFd] = ResponseInfo();
  }
}

void HttpServer::SendData()
{
  fd_set selectFds;
  FD_ZERO(&selectFds);
  
  int maxFd = 0;
  
  for (auto client: _beingServedClients) {
    int clientFd = client.first;
    
    FD_SET(clientFd, &selectFds);
    
    if (clientFd > maxFd) {
      maxFd = clientFd;
    }
  }
  
  if (0 == maxFd) {
    return;
  }
  
  timeval selectTimeSpec = { 0 };
  
  int result = ::select(maxFd + 1, nullptr, &selectFds, nullptr, &selectTimeSpec);
  if (result < 0 && errno != EINTR) {
    Tracer::LogErrNo("select().\n");
    return;
  }
  
  if (0 == result) {
    return;
  }
  
  std::list<int> brokenClientFds;
  
  for (std::pair<const int, ResponseInfo>& client : _beingServedClients) {
    int clientFd = client.first;
    
    if (FD_ISSET(clientFd, &selectFds)) {
      ResponseInfo& responseInfo = client.second;
      
      bool shouldBreak = false;
      
      while (!shouldBreak) {
        if (responseInfo.HeaderBytesSent < HeaderSize) {
          int writeResult = ::write(clientFd, HttpHeader + responseInfo.HeaderBytesSent, HeaderSize - responseInfo.HeaderBytesSent);
          if (writeResult > 0) {
            responseInfo.HeaderBytesSent += writeResult;
          }
          else if (-1 == writeResult && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Stop sending data to the current client as it is busy.
            shouldBreak = true;
          }
          else {
            // Stop sending data to the current client as something went wrong.
            shouldBreak = true;
            Tracer::LogErrNo("write().\n");
            brokenClientFds.push_back(clientFd);
          }
        }      
        else if (ResponseInfo::InvalidBufferIdx == responseInfo.VideoFrameIdx) {
          HttpServer::QueueItem* queueItem = SelectBufferForSending(responseInfo.Timestamp);
          if (queueItem != nullptr) {
            queueItem->UsageCounter += 1;
            queueItem->SentCounter += 1;
            
            responseInfo.DataBufferBytesSent = 0;
            responseInfo.DataBufferIdx = 0;
            responseInfo.Timestamp = queueItem->SourceData->V4l2Buffer.timestamp;
            responseInfo.VideoFrameIdx = queueItem->SourceData->Idx;
          }
          else {
            // Stop sending data to the current client as there is no data for sending.
            shouldBreak = true;
          }
        }
        else {
          HttpServer::QueueItem* queueItem = GetBuffer(responseInfo.VideoFrameIdx);
          
          if (nullptr == queueItem) {
            // Stop sending data to the current client as unexpected problem is detected.
            shouldBreak = true;
          }
          else {
            const Buffer& buffer = queueItem->Data[responseInfo.DataBufferIdx];
            int writeResult = ::write(clientFd, buffer.Data + responseInfo.DataBufferBytesSent,
                                      buffer.Size - responseInfo.DataBufferBytesSent);
            if (writeResult > 0) {
              responseInfo.DataBufferBytesSent += writeResult;
              if (responseInfo.DataBufferBytesSent == buffer.Size) {
                if (responseInfo.DataBufferIdx + 1 >= queueItem->Data.size()) {

                  // The item was sent so we need to find a new item
                  responseInfo.VideoFrameIdx = ResponseInfo::InvalidBufferIdx;
                  
                  // Release the item.
                  queueItem->UsageCounter -= 1;

//                   // Check the max transmission rate
//                   responseInfo.HeaderBytesSent = 0;
//                   responseInfo.DataBufferIdx = 0;
//                   responseInfo.DataBufferBytesSent = 0; 
//                   shouldBreak = true;
                }
                else {
                  // Switch to next buffer
                  responseInfo.DataBufferIdx += 1;
                  responseInfo.DataBufferBytesSent = 0;             
                }
              }
            }
            else if (-1 == writeResult && (errno == EAGAIN || errno == EWOULDBLOCK)) {
              shouldBreak = true;
            }
            else {
              // Stop sending data to the current client as something went wrong.
              shouldBreak = true;
              Tracer::LogErrNo("write().\n");
              brokenClientFds.push_back(clientFd);

              queueItem->UsageCounter -= 1;
            }        
          }
        }
      }
    }
  }
  
  for (auto clientFd : brokenClientFds) {
    _beingServedClients.erase(clientFd);
    
    if (-1 == ::close(clientFd)) {
      Tracer::LogErrNo("close().\n");
    }
  }  
}

HttpServer::QueueItem* HttpServer::SelectBufferForSending(const timeval& lastBufferTimestamp)
{
  for (auto it = _incomeQueue.begin(); it != _incomeQueue.end(); it++) {
    const timeval& itemTimestamp = it->SourceData->V4l2Buffer.timestamp;
    if (itemTimestamp.tv_sec > lastBufferTimestamp.tv_sec ||
      (itemTimestamp.tv_sec == lastBufferTimestamp.tv_sec && itemTimestamp.tv_usec > lastBufferTimestamp.tv_usec)) {
      return &(*it);
    }
  }
  
  // Strange: sometimes under debugger new frames can get very strange timestamps
  if (!_incomeQueue.empty()) {
    QueueItem& newestFrame = _incomeQueue.back();
    if (newestFrame.SourceData->V4l2Buffer.timestamp.tv_sec < lastBufferTimestamp.tv_sec) {
      return &newestFrame;
    }
  }
  
  return nullptr;
}

HttpServer::QueueItem* HttpServer::GetBuffer(uint32_t videoBufferIdx)
{
  for (QueueItem& queueItem : _incomeQueue) {
    if (queueItem.SourceData->Idx == videoBufferIdx) {
      return &queueItem;
    }
  }
  
  return nullptr;
}

bool HttpServer::QueueFrame(const VideoFrame* videoBuffer)
{
  std::vector<Buffer> mjpegFrameData = CreateMjpegFrameBufferSet(videoBuffer);
  if (mjpegFrameData.empty()) {
    return false;
  }
  
  uint32_t frameSize = 0;
  for (const Buffer& buff : mjpegFrameData) {
    frameSize += buff.Size;
  }
  
  static const char frameHeaderTemplate[] = 
    "Content-Type: image/jpeg\r\n" \
    "Content-Length: %d\r\n" \
    "X-Timestamp: %ld.%06ld\r\n" \
    "\r\n";

  std::vector<uint8_t> frameHeaderBuff(sizeof(frameHeaderTemplate) + 100, 0);
  int result = std::snprintf(reinterpret_cast<char*>(frameHeaderBuff.data()),
                        frameHeaderBuff.size() - 1, frameHeaderTemplate, 
                        frameSize, videoBuffer->V4l2Buffer.timestamp.tv_sec,
                        videoBuffer->V4l2Buffer.timestamp.tv_usec);
  if (result <= 0) {
    Tracer::Log("Failed to create HTTP header for MJPEG frame: snprintf.\n");
    return false;
  }
  
  uint32_t headerSize = static_cast<uint32_t>(result);
  if (headerSize > frameHeaderBuff.size()) {
    Tracer::Log("Failed to create HTTP header for MJPEG frame. buffer is \n");
    return false;
  }

  QueueItem newFrame;
  newFrame.Header.swap(frameHeaderBuff);

  newFrame.Data.reserve(mjpegFrameData.size() + 2);
  newFrame.Data.push_back(Buffer {newFrame.Header.data(), headerSize});
  newFrame.Data.insert(newFrame.Data.end(), mjpegFrameData.begin(), mjpegFrameData.end());
  newFrame.Data.push_back(Buffer {HttpBoundaryValue, sizeof(HttpBoundaryValue) - 1});
  newFrame.SourceData = videoBuffer;
  newFrame.UsageCounter = 0;
  newFrame.SentCounter = 0;

//   const uint32_t missedFrames = _incomeQueue.empty() ? 0 : newFrame.SourceData->V4l2Buffer.sequence - (_incomeQueue.back().SourceData->V4l2Buffer.sequence + 1);
//   if (missedFrames != 0) {
//     Tracer::Log("HttpServer missed %d frame[s] before frame %d\n", missedFrames, newFrame.SourceData->V4l2Buffer.sequence);
//   }
  
  _incomeQueue.push_back(std::move(newFrame));
  
  return true;
}

const VideoFrame* HttpServer::DequeueFrame(bool force)
{
  auto standardDequeCondition = [](const QueueItem& queueItem) -> bool { 
    return (queueItem.UsageCounter == 0) && (queueItem.SentCounter != 0); 
  };

  auto forcedDequeCondition = [](const QueueItem& queueItem) -> bool {
    return (queueItem.UsageCounter == 0);
  };
  
  auto dequeCondition = force ? forcedDequeCondition : standardDequeCondition;

  auto it = std::find_if(_incomeQueue.begin(), _incomeQueue.end(), dequeCondition);
  if (it != _incomeQueue.end()) {
    size_t notSentFramesNumber = std::count_if(_incomeQueue.begin(), _incomeQueue.end(), dequeCondition);
    if (force || notSentFramesNumber > 1) {
      if (/*!_beingServedClients.empty() &&*/ (0 == it->SentCounter)) {
        Tracer::Log("HttpServer skipped frame %d %ld.%06ld\n",
                    it->SourceData->V4l2Buffer.sequence,
                    it->SourceData->V4l2Buffer.timestamp.tv_sec,
                    it->SourceData->V4l2Buffer.timestamp.tv_usec);
      }

      const VideoFrame* dequeuedFrame = it->SourceData;

      _incomeQueue.erase(it);
    
      return dequeuedFrame;
    }
  }
 
  return nullptr;
}

std::vector<const VideoFrame*> HttpServer::DequeueAllFrames()
{
  std::vector<const VideoFrame*> result;

  result.reserve(_incomeQueue.size());
  for (auto item : _incomeQueue) {
    result.push_back(item.SourceData);
  }
  
  // We have to wait till all currenty being transmitted buffers are sent.
  auto isUnused = [](const QueueItem& queueItem) -> bool {
    return 0 == queueItem.UsageCounter; 
  };
  
  static const unsigned int MaxAttempts = 5U;
  for (unsigned int attempt = 0; attempt < MaxAttempts; ++attempt) {
    // Send already being transmitted VideoFrame-s
    SendData();

    // Remove all unused VideoFrames from _incomeQueue.
    _incomeQueue.remove_if(isUnused);
    
    if (_incomeQueue.empty()) {
      break;
    }
  }
  
  if (!_incomeQueue.empty()) {
    std::list<int> brokenClientFds;
    for (auto client : _beingServedClients) {
      ResponseInfo& responseInfo = client.second;
      if (responseInfo.VideoFrameIdx != ResponseInfo::InvalidBufferIdx) {
        HttpServer::QueueItem* queueItem = GetBuffer(responseInfo.VideoFrameIdx);
          
        if (queueItem != nullptr) {
          responseInfo.VideoFrameIdx = ResponseInfo::InvalidBufferIdx;
          queueItem->UsageCounter -= 1;
        }
        
        brokenClientFds.push_back(client.first);
      }
    }  
    
    for (auto clientFd : brokenClientFds) {
      _beingServedClients.erase(clientFd);
    
      if (-1 == ::close(clientFd)) {
        Tracer::LogErrNo("close().\n");
      }
      
      Tracer::Log("Closed slow client.");
    }  
  }
  
  _incomeQueue.remove_if(isUnused);
  
  return result;
}


namespace {

  bool SetupListeningSocket(int socketFd, const addrinfo* addrInfo, int maxPendingConnections)
  {
    // Ignore "socket already in use" errors 
    int reuseAddrValue = 1;
    if (::setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &reuseAddrValue, sizeof(reuseAddrValue)) != 0) {
      Tracer::LogErrNo("setsockopt(SO_REUSEADDR).\n");
      return false;
    }
    
    // Switch to non-blocking mode
    if (::fcntl(socketFd, F_SETFL, O_NONBLOCK) < 0) {
      Tracer::LogErrNo("fcntl(F_SETFL, O_NONBLOCK).\n");
      return false;
    }
    
    if (::bind(socketFd, addrInfo->ai_addr, addrInfo->ai_addrlen) != 0) {
      Tracer::LogErrNo("bind().\n");
      return false;
    }
    
    if (::listen(socketFd, maxPendingConnections) != 0) {
      Tracer::LogErrNo("listen().\n");
      return false;
    }
    
    return true;
  }

  int CreateListeningSocket(const addrinfo* addrInfo)
  {
    static const int MaxPendingConnections = 4;

    int socketFd = ::socket(addrInfo->ai_family, addrInfo->ai_socktype, 0);
    if (-1 == socketFd) {
      Tracer::LogErrNo("Failed to create a socket.\n");
      return -1;
    }
    
    if (!SetupListeningSocket(socketFd, addrInfo, MaxPendingConnections)) {
      ::close(socketFd);
      Tracer::Log("Failed to configure a listening socket.\n");
      return -1;
    }
    
    return socketFd;
  }
}
