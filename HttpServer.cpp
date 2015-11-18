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
  : _listeningFds(0)
{
  _listeningFds.reserve(MaxServersNum);
}

HttpServer::~HttpServer()
{
  Shutdown();
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

void HttpServer::ServeRequests(long maxServeTimeMicroSec)
{
  // Send data to connected clients

  if (!_beingServedClients.empty()) {
    SendData(maxServeTimeMicroSec);
  }    

  // If there is only one client then we have to send all grabbed data to it.
  while (_beingServedClients.size() == 1 && 
    _beingServedClients.cbegin()->second.VideoBufferIdx != ResponseInfo::InvalidBufferIdx) {
    
    SendData(maxServeTimeMicroSec);
  }
  
  
  // Check for new connections.
  
  // Count and accept only for every 100th call.
  static uint32_t listenCount = 0;
  listenCount %= 1000;
  listenCount += 1;
  if ((listenCount % 100) == 0) {
    return;
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

  timeval selectTimeSpec = { 0 };
  int result = ::select(maxFd + 1, &selectfds, nullptr, nullptr, &selectTimeSpec);
  if (result < 0 && errno != EINTR) {
    Tracer::LogErrNo("select().");
    return;
  }
  
  for (size_t i = 0; i < _listeningFds.size(); ++i) {
    if (_listeningFds[i] != -1 && FD_ISSET(_listeningFds[i], &selectfds)) {
      
      sockaddr_storage sockAddr = {0};
      socklen_t sockAddrSize = sizeof(sockAddr);
      
      int clientFd = ::accept(_listeningFds[i], reinterpret_cast<sockaddr*>(&sockAddr), &sockAddrSize);
      if (-1 != clientFd) {
        if (GetClientsNumber() < MaxClientsNum) {
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
        Tracer::LogErrNo("accept().");
      }
    }
  }
  
  ReadAndParseRequests();
}

bool HttpServer::HasDataToSend() const
{
  for (auto clientIt : _beingServedClients) {
    
    const ResponseInfo& responseInfo = clientIt.second;
    
    if (responseInfo.HeaderBytesSent < HeaderSize ||
      responseInfo.DataBufferIdx != ResponseInfo::InvalidBufferIdx) {
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
  
  for (auto clientFdIt : _waitingClients) {
    int clientFd = clientFdIt.first;
    
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
    Tracer::LogErrNo("select().");
    
    // Something wrong happened with waiting clients. Drop all of them.
    
    for (auto clientFdIt : _waitingClients) {
      result = ::close(clientFdIt.first);
      if (result < 0) {
        Tracer::LogErrNo("close().");
      }
    }
    
    _waitingClients.clear();
    return;
  }
  
  std::list<int> brokenClientFds;
  std::list<int> parsedClientFds;
  
  for (auto clientFdIt : _waitingClients) {
    int clientFd = clientFdIt.first;
    RequestInfo& requestInfo = clientFdIt.second;
    
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
      Tracer::LogErrNo("close().");
    }
  }
  
  for (auto parsedClientFd : parsedClientFds) {
    _waitingClients.erase(parsedClientFd);
    _beingServedClients[parsedClientFd] = ResponseInfo();
  }
}

void HttpServer::SendData(long timeoutMicroSec)
{
  fd_set selectFds;
  FD_ZERO(&selectFds);
  
  int maxFd = 0;
  
  for (auto clientFdIt : _beingServedClients) {
    int clientFd = clientFdIt.first;
    
    FD_SET(clientFd, &selectFds);
    
    if (clientFd > maxFd) {
      maxFd = clientFd;
    }
  }
  
  if (0 == maxFd) {
    return;
  }
  
  timeval selectTimeSpec = {0, timeoutMicroSec};
  
  int result = ::select(maxFd + 1, nullptr, &selectFds, nullptr, &selectTimeSpec);
  if (result < 0 && errno != EINTR) {
    Tracer::LogErrNo("select().");
    return;
  }
  
  if (0 == result) {
    return;
  }
  
  std::list<int> brokenClientFds;
  
  for (std::pair<const int, ResponseInfo>& beingServedClientsIt : _beingServedClients) {
    int clientFd = beingServedClientsIt.first;
    
    if (FD_ISSET(clientFd, &selectFds)) {
      ResponseInfo& responseInfo = beingServedClientsIt.second;
      
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
            Tracer::LogErrNo("write().");
            brokenClientFds.push_back(clientFd);
          }
        }      
        else if (ResponseInfo::InvalidBufferIdx == responseInfo.VideoBufferIdx) {
          HttpServer::QueueItem* queueItem = SelectBufferForSending(responseInfo.Timestamp);
          if (queueItem != nullptr) {
            queueItem->UsageCounter += 1;
            queueItem->SentCounter += 1;
            
            responseInfo.DataBufferBytesSent = 0;
            responseInfo.DataBufferIdx = 0;
            responseInfo.Timestamp = queueItem->SourceData->V4l2Buffer.timestamp;
            responseInfo.VideoBufferIdx = queueItem->SourceData->Idx;
          }
          else {
            // Stop sending data to the current client as there is no data for sending.
            shouldBreak = true;
          }
        }
        else {
          HttpServer::QueueItem* queueItem = GetBuffer(responseInfo.VideoBufferIdx);
          
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
                  responseInfo.VideoBufferIdx = ResponseInfo::InvalidBufferIdx;
                  
                  // Release the item.
                  queueItem->UsageCounter -= 1;
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
              Tracer::LogErrNo("write().");
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
      Tracer::LogErrNo("close().");
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

bool HttpServer::QueueBuffer(const VideoBuffer* videoBuffer)
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

  if (!_incomeQueue.empty() && _incomeQueue.back().SourceData->V4l2Buffer.sequence + 1 < newFrame.SourceData->V4l2Buffer.sequence) {
    Tracer::Log("HttpServer missed frame[s] before frame %d\n", newFrame.SourceData->V4l2Buffer.sequence);
  }
  
  _incomeQueue.push_back(std::move(newFrame));
  
  return true;
}

const VideoBuffer* HttpServer::DequeueBuffer(bool force)
{
  auto currIt = _incomeQueue.begin();
  while (currIt != _incomeQueue.end()) {

    if (currIt != _incomeQueue.end() && 0 == currIt->UsageCounter && (currIt->SentCounter != 0 || force)) {
      const VideoBuffer* dequeuedBuffer = currIt->SourceData;
      _incomeQueue.erase(currIt);

      if (!_beingServedClients.empty() && 0 == currIt->SentCounter) {
        Tracer::Log("HttpServer skipped frame %d %ld.%06ld\n",
                    dequeuedBuffer->V4l2Buffer.sequence,
                    dequeuedBuffer->V4l2Buffer.timestamp.tv_sec,
                    dequeuedBuffer->V4l2Buffer.timestamp.tv_usec);
      }

      return dequeuedBuffer;
    }
      
    ++currIt;
  }
  
  return nullptr;
//     auto nextIt = currIt;
//     ++nextIt;
//     
//     if (nextIt != _incomeQueue.end() && 0 == currIt->UsageCounter) {
//       const VideoBuffer* dequeuedBuffer = currIt->SourceData;
// 
// This code is for debugging slow clients      
//       if (!_beingServedClients.empty()) {
//         if (0 == currIt->SentCounter) {
//           Tracer::Log("HttpServer missed frame %d %ld.%06ld\n",
//                  dequeuedBuffer->V4l2Buffer.sequence,
//                  dequeuedBuffer->V4l2Buffer.timestamp.tv_sec,
//                  dequeuedBuffer->V4l2Buffer.timestamp.tv_usec);
//         }
//         
//         static timeval lastSentTs = dequeuedBuffer->V4l2Buffer.timestamp;
//         
//         long int delta = 0;
//         long int seconds = dequeuedBuffer->V4l2Buffer.timestamp.tv_sec - lastSentTs.tv_sec;
//         
//         if (seconds > 0) {
//           delta = seconds * 1000000;
//           delta -= lastSentTs.tv_usec;
//           delta += dequeuedBuffer->V4l2Buffer.timestamp.tv_usec;
//         }
//         else {
//           delta = dequeuedBuffer->V4l2Buffer.timestamp.tv_usec - lastSentTs.tv_usec;
//         }
//         
//         if (delta > 40000) {
//           Tracer::Log("Latency detected for frame %d: %ld\n", dequeuedBuffer->V4l2Buffer.sequence, delta);
//         }
//         
//         lastSentTs = dequeuedBuffer->V4l2Buffer.timestamp;
//       }
//       
//       _incomeQueue.erase(--currIt.base());
//       return dequeuedBuffer;
//     }
//     
//     currIt = nextIt;
}

std::vector<const VideoBuffer*> HttpServer::DequeueAllBuffers()
{
  std::vector<const VideoBuffer*> result;
  result.reserve(_incomeQueue.size());
  for (auto item : _incomeQueue) {
    result.push_back(item.SourceData);
  }
  
  // We have to wait till all currenty being transmitted buffers are sent.
  
  static const unsigned int SleepTimeInUSec = 5000;
  static const unsigned int MaxTotalSleepTimeInUs = 500000U;
  static const unsigned int MaxAttempts = MaxTotalSleepTimeInUs / SleepTimeInUSec;
  
  auto isUnused = [](const QueueItem& queueItem) -> bool {
    return 0 == queueItem.UsageCounter; 
  };
  
  for (unsigned int attempt = 0; attempt < MaxAttempts && !_incomeQueue.empty(); ++attempt) {
    // Remove all unused VideoBuffers from _incomeQueue.
    _incomeQueue.remove_if(isUnused);
    
    // Send already being transmitted VideoBuffer-s
    SendData(SleepTimeInUSec);
  }
  
  if (!_incomeQueue.empty()) {
    std::list<int> brokenClientFds;
    for (auto client : _beingServedClients) {
      ResponseInfo& responseInfo = client.second;
      if (responseInfo.VideoBufferIdx != ResponseInfo::InvalidBufferIdx) {
        HttpServer::QueueItem* queueItem = GetBuffer(responseInfo.VideoBufferIdx);
          
        if (queueItem != nullptr) {
           responseInfo.VideoBufferIdx = ResponseInfo::InvalidBufferIdx;
           queueItem->UsageCounter -= 1;
        }
        
        brokenClientFds.push_back(client.first);
      }
    }  
    
    for (auto clientFd : brokenClientFds) {
      _beingServedClients.erase(clientFd);
    
      if (-1 == ::close(clientFd)) {
        Tracer::LogErrNo("close().");
      }
      
      Tracer::Log("Closed slow client.");
    }  
  }
  
  _incomeQueue.remove_if(isUnused);
  
  return result;
}

void HttpServer::Shutdown()
{
  DequeueAllBuffers();
  
  for (auto client : _beingServedClients) {
    if (-1 == ::close(client.first)) {
      Tracer::LogErrNo("close().");
    }
  }  
  _beingServedClients.clear();
  
  for (auto client : _waitingClients) {
    if (-1 == ::close(client.first)) {
      Tracer::LogErrNo("close().");
    }
  }  
  
  for (auto fd : _listeningFds) {
    if (-1 == ::close(fd)) {
      Tracer::LogErrNo("close().");
    }
  } 
  
  _listeningFds.clear();
}

namespace {

  bool SetupListeningSocket(int socketFd, const addrinfo* addrInfo, int maxPendingConnections)
  {
    // Ignore "socket already in use" errors 
    int reuseAddrValue = 1;
    if (::setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &reuseAddrValue, sizeof(reuseAddrValue)) != 0) {
      Tracer::LogErrNo("setsockopt(SO_REUSEADDR).");
      return false;
    }
    
    // Switch to non-blocking mode
    if (::fcntl(socketFd, F_SETFL, O_NONBLOCK) < 0) {
      Tracer::LogErrNo("fcntl(F_SETFL, O_NONBLOCK).");
      return false;
    }
    
    if (::bind(socketFd, addrInfo->ai_addr, addrInfo->ai_addrlen) != 0) {
      Tracer::LogErrNo("bind().");
      return false;
    }
    
    if (::listen(socketFd, maxPendingConnections) != 0) {
      Tracer::LogErrNo("listen().");
      return false;
    }
    
    return true;
  }

  int CreateListeningSocket(const addrinfo* addrInfo)
  {
    static const int MaxPendingConnections = 4;

    int socketFd = ::socket(addrInfo->ai_family, addrInfo->ai_socktype, 0);
    if (-1 == socketFd) {
      Tracer::LogErrNo("Failed to create a socket.");
      return -1;
    }
    
    if (!SetupListeningSocket(socketFd, addrInfo, MaxPendingConnections)) {
      ::close(socketFd);
      Tracer::Log("Failed to configure a listening socket.");
      return -1;
    }
    
    return socketFd;
  }
}
