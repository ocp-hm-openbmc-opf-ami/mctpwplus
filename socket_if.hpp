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
    SocketInterface(const std::string_view& socketPath,
                    boost::asio::io_context& io);
    inline void setMessageReceivedCallback(ReceiveMessageCallback cb)
    {
        onMessageReceived = cb;
    }
    ~SocketInterface();

  private:
    BoostSocket socket;
    std::unordered_map<int, std::shared_ptr<boost::asio::steady_timer>>
        reqTimerList;
    ByteArray pendingRsp;
    boost::asio::streambuf buffer;
    ReceiveMessageCallback onMessageReceived = nullptr;
    void startReceiving();
    void onSocketReceive(const boost::system::error_code& error,
                         std::size_t bytesTransferred);
};
} // namespace mctpw
