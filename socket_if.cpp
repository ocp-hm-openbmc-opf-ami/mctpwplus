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
#include "socket_if.hpp"

#include "protocol.hpp"

#include <phosphor-logging/log.hpp>
using mctpw::SocketInterface;

template <typename T>
void writeSocket(boost::asio::local::stream_protocol::socket& socket,
                 const T& data, size_t len)
{

    boost::asio::write(socket, boost::asio::buffer(data.data(), len));
}

SocketInterface::SocketInterface(const std::string_view& socketPath,
                                 boost::asio::io_context& io) :
    socket(io), ioc(io)
{
    constexpr char unixSktAbsPath[] = "\0mctp";
    constexpr size_t unixSktAbsPathLen = sizeof(unixSktAbsPath) - 1;
    std::string path(unixSktAbsPath, unixSktAbsPathLen);
    path += socketPath;
    socket.connect(UnixSocket::endpoint(socketPath));
    startReceiving();
}

SocketInterface::~SocketInterface()
{
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);

    socket.close();
}

using It = boost::asio::buffers_iterator<boost::asio::const_buffers_1>;

std::pair<It, bool> isCompleteRequest(It begin, It end)
{
    auto distance = std::distance(begin, end);
    if (distance > std::numeric_limits<uint16_t>::max())
    {
        return std::make_pair(begin, false);
    }

    if (distance < static_cast<int>(sizeof(internal::UnixIPCMessage)))
    {
        return std::make_pair(begin, false);
    }

    internal::UnixIPCMessage msg;
    std::copy(begin, std::next(begin, sizeof(msg)),
              reinterpret_cast<uint8_t*>(&msg));
    auto expectedSize = le16toh(msg.len);
    if (distance >= expectedSize)
    {
        return std::make_pair(std::next(begin, expectedSize), true);
    }
    else
    {
        return std::make_pair(begin, false);
    }
}

void SocketInterface::startReceiving()
{
    boost::asio::async_read_until(socket, buffer, isCompleteRequest,
                                  std::bind(&SocketInterface::onSocketReceive,
                                            this, std::placeholders::_1,
                                            std::placeholders::_2));
}
void SocketInterface::onSocketReceive(const boost::system::error_code& ec,
                                      std::size_t size)
{

    if (ec == boost::asio::error::eof ||
        ec == boost::asio::error::bad_descriptor)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "UNIX socket disconnected");
        return;
    }
    if (ec)
    {
        startReceiving();
        return;
    }
    std::vector<uint8_t> rspBuf(size);
    boost::asio::buffer_copy(boost::asio::buffer(rspBuf), this->buffer.data(),
                             size);
    buffer.consume(size);
    auto msg = reinterpret_cast<internal::UnixIPCMessage*>(rspBuf.data());
    if (msg->opCode == internal::OpCode::directedResponse)
    {
        pendingRsp = std::move(rspBuf);
        auto timer = reqTimerList[msg->sqNum];
        timer->cancel();
        reqTimerList.erase(msg->sqNum);
    }
    else
    {
        if (onMessageReceived)
        {
            auto broadcastMsg = reinterpret_cast<internal::BroadcastMessage*>(
                rspBuf.data() + sizeof(*msg));
            auto msgSize = sizeof(*msg) + sizeof(*broadcastMsg);

            if (rspBuf.size() >= msgSize)
            {
                auto payloadLen = rspBuf.size() - msgSize;
                std::vector<uint8_t> payload(
                    std::prev(rspBuf.end(), payloadLen), rspBuf.end());
                onMessageReceived(msg->eid, broadcastMsg->tagOwner,
                                  broadcastMsg->msgTag, payload);
            }
        }
    }
    startReceiving();
}

template <typename Arr, typename T>
void prefixBytes(Arr& a, const T& t)
{
    using Val = typename Arr::value_type;
    auto begin = reinterpret_cast<const Val*>(&t);
    auto end = begin + sizeof(t);
    a.insert(a.begin(), begin, end);
}

std::pair<std::error_code, SocketInterface::ByteArray>
    SocketInterface::sendReceiveYield(boost::asio::yield_context yield,
                                      uint8_t eid, ByteArray req,
                                      const std::chrono::milliseconds timeout)
{
    ByteArray rsp{};
    internal::UnixIPCMessage msg;
    internal::SendReceiveRequest sendRcvReq;
    msg.eid = eid;
    msg.sqNum = ++seqNum;
    msg.errorCode = 0;
    msg.len = htole16(
        static_cast<uint16_t>(req.size() + sizeof(msg) + sizeof(sendRcvReq)));
    msg.opCode = internal::OpCode::sendReceive;
    sendRcvReq.timeout = static_cast<uint16_t>(timeout.count());
    prefixBytes(req, sendRcvReq);
    prefixBytes(req, msg);
    writeSocket(socket, req, req.size());
    auto it = reqTimerList.find(seqNum);
    if (it != reqTimerList.end())
    {
        return std::make_pair(
            std::make_error_code(std::errc::device_or_resource_busy), rsp);
    }
    auto timer = std::make_shared<boost::asio::steady_timer>(ioc);
    timer->expires_from_now(timeout);
    reqTimerList.insert(std::make_pair(seqNum, timer));
    boost::system::error_code ec;
    timer->async_wait(yield[ec]);
    if (ec == boost::asio::error::operation_aborted)
    {
        if (req.size() == 0 ||
            pendingRsp.size() < sizeof(internal::UnixIPCMessage))
        {
            return std::make_pair(
                std::make_error_code(std::errc::no_message_available), rsp);
        }
        auto error =
            reinterpret_cast<internal::UnixIPCMessage*>(pendingRsp.data())
                ->errorCode;
        pendingRsp.erase(
            pendingRsp.begin(),
            std::next(pendingRsp.begin(), sizeof(internal::UnixIPCMessage)));
        return std::make_pair(std::error_code(error, std::generic_category()),
                              pendingRsp);
    }
    return std::make_pair(std::make_error_code(std::errc::timed_out), rsp);
}