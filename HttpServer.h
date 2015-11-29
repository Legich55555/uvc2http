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

#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <map>
#include <list>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include "Buffer.h"

/** @brief HttpServer implements minimal HTTP server for sending MJPEG frames.
 */
class HttpServer
{
public:
  
  HttpServer();
  ~HttpServer();

  bool Init(const char* servicePort);
  
  /** @brief Shutdowns all connections and frees all resources. 
   */
  void Shutdown();
  
  /** @brief Adds a buffer to a queue "to be sent". 
   *
   *  @return Returns true if buffer was successfully queued.
   */
  bool QueueFrame(const VideoFrame* videoBuffer);
  
  /** @brief Dequeues a buffer.
   *
   *  @return A buffer which was dequeued or nullptr. 
   */
  const VideoFrame* DequeueFrame(bool force);
  
  /** @brief Dequeues all buffers. 
   *
   *  If a client connection is slow then it is dropped.
   */
  std::vector<const VideoFrame*> DequeueAllFrames();
  
  HttpServer(const HttpServer& other) = delete;
  HttpServer& operator=(const HttpServer& other) = delete;
  
private:
  
  struct RequestInfo 
  {
    std::vector<uint8_t> RequestData;
  };

  struct ResponseInfo
  {
    uint32_t HeaderBytesSent = 0U;
    
    static const uint32_t InvalidBufferIdx = 0xFFFFFFFF;
    
    uint32_t DataBufferIdx = InvalidBufferIdx;
    uint32_t DataBufferBytesSent = 0U;
    timeval Timestamp = {0};
    uint32_t VideoFrameIdx = InvalidBufferIdx;
  }; 
  
  struct QueueItem {
    std::vector<uint8_t> Header;
    std::vector<Buffer> Data;
    const VideoFrame* SourceData;
    uint32_t UsageCounter;
    uint32_t SentCounter;
  };
  
  void WorkFunc();

  /** @brief Returns true if there are data for client (headers or MJPEG data). 
   */
  bool HasDataToSend() const;

  std::size_t GetClientsNumber() const { return _beingServedClients.size() + _waitingClients.size(); }
  
  /** @brief Accepts a new requests, sends images to active peers. 
   */
  void ServeRequests();
  
  void ReadAndParseRequests();
  void SendData();
  QueueItem* SelectBufferForSending(const timeval& lastBufferTimestamp);
  QueueItem* GetBuffer(uint32_t videoBufferIdx);
  
  std::map<int, RequestInfo> _waitingClients;
  std::vector<int> _listeningFds;
  std::map<int, ResponseInfo> _beingServedClients;
  std::list<QueueItem> _incomeQueue; 
  
  std::thread _workedThread;
  mutable std::mutex _queueMutex;
  std::condition_variable _wakeUpSending;
  std::mutex _wakeUpMutex;
  
  std::atomic<bool> _needToStop;
};

#endif // HTTPSERVER_H
