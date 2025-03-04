/*
// Copyright (c) 2024 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#pragma once

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

using It = boost::asio::buffers_iterator<boost::asio::const_buffer>;
std::pair<It, bool> isCompleteRequest(It begin, It end);

namespace mctpw
{
class SocketInterface
{
  public:
    using BoostSocket = boost::asio::local::stream_protocol::socket;
    using UnixSocket = boost::asio::local::stream_protocol;
    using ByteArray = std::vector<uint8_t>;
    using ReceiveMessageCallback =
        std::function<void(uint8_t, bool, uint8_t, const ByteArray&)>;
    using ReceiveCallback =
        std::function<void(boost::system::error_code, ByteArray&)>;
    using SendCallback = std::function<void(boost::system::error_code, int)>;

    SocketInterface(const std::string_view& socketPath,
                    boost::asio::io_context& io);
    inline void setMessageReceivedCallback(ReceiveMessageCallback cb)
    {
        onMessageReceived = cb;
    }
    std::pair<std::error_code, ByteArray>
        sendReceiveYield(boost::asio::yield_context yield, uint8_t eid,
                         ByteArray req,
                         const std::chrono::milliseconds timeout);
    void sendReceiveAsync(ReceiveCallback receiveCb, uint8_t dstEId,
                          ByteArray request, std::chrono::milliseconds timeout);
    void sendAsync(const SendCallback& callback, const uint8_t dstEId,
                   const uint8_t msgTag, const bool tagOwner,
                   ByteArray request);
    ~SocketInterface();

  private:
    BoostSocket socket;
    boost::asio::io_context& ioc;
    std::unordered_map<int, std::shared_ptr<boost::asio::steady_timer>>
        reqTimerList;
    ByteArray pendingRsp;
    int seqNum = 0;
    boost::asio::streambuf buffer;
    ReceiveMessageCallback onMessageReceived = nullptr;
    void startReceiving();
    void onSocketReceive(const boost::system::error_code& error,
                         std::size_t bytesTransferred);
};
} // namespace mctpw