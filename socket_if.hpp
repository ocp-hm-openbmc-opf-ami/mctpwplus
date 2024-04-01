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
    using ByteArray = std::vector<uint8_t>;
    SocketInterface(const std::string_view& socketPath,
                    boost::asio::io_context& io);

    ~SocketInterface();

  private:
    BoostSocket socket;
    boost::asio::deadline_timer reqTimer;
};
} // namespace mctpw
