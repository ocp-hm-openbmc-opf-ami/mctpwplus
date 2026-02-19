/*
// Copyright (c) 2025 Intel Corporation
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
#include "mctp_impl.hpp"

#include "utils.hpp"

#include <linux/mctp.h>
#include <sys/socket.h>

#include <boost/algorithm/string.hpp>
#include <fstream>
#include <iostream>
#include <phosphor-logging/log.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus/match.hpp>
#include <unordered_set>

namespace mctpw
{

/* objectPath: string  in format "<some
 * text>/networks/<networkID>/endpoints/<endpointID>" returns DeviceId object
 * with networkID and endpointID as in objectPath
 */
static std::optional<DeviceID>
    extractDeviceIDFromObjectPath(const std::string& objectPath)
{
    static const std::string network("networks");
    static const std::string endpoint("endpoints");
    auto networkIDItrBegin =
        objectPath.begin() + objectPath.find(network) + network.size() + 1;
    if (networkIDItrBegin >= objectPath.end())
    {
        return std::nullopt;
    }
    auto networkIDItrEnd = objectPath.begin();
    auto tmp = objectPath.find("/", networkIDItrBegin - objectPath.begin());
    if (tmp != std::string::npos)
    {
        networkIDItrEnd += tmp;
    }
    else
    {
        return std::nullopt;
    }
    std::string networkIDStr(networkIDItrBegin, networkIDItrEnd);
    auto endpointIDBegin =
        objectPath.begin() + objectPath.find(endpoint) + endpoint.size() + 1;
    if (endpointIDBegin >= objectPath.end())
    {
        return std::nullopt;
    }
    std::string endpointIDStr(endpointIDBegin, objectPath.end());
    NetworkID networkID = static_cast<uint8_t>(std::stol(networkIDStr));
    eid_t endpointID = static_cast<uint8_t>(std::stol(endpointIDStr));
    if ((networkIDStr != std::to_string(networkID)) ||
        (endpointIDStr != std::to_string(endpointID)))
    {
        return std::nullopt;
    }
    return std::optional<DeviceID>(DeviceID(endpointID, networkID));
}

/* List of message types for which mctpwplus will bind. SPDM and vendor
 * defined message types are not included in this list since seperate
 * services is binding to these message type
 */
const std::unordered_set<MessageType> permittedMessageTypes{
    MessageType::pldm,        MessageType::ncsi,     MessageType::ethernet,
    MessageType::nvmeMgmtMsg, MessageType::cxlFmApi, MessageType::cxlCci};

/* MCTPImpl object keeps track of all endpoints in system however only subset of
 * these endpoints is suitable for invoking ReconfigurationCallback.
 * In other words this method checks if given endpoint is suitable for mctpwplus
 * consumer.
 */
bool MCTPImpl::eligibleForReconfigurationCallback(const EndpointInfo& epInfo)
{
    if (epInfo.selfEndpoint)
    {
        return false;
    }
    bool messageTypeMatched = (config.type == MessageType::any);
    if (!messageTypeMatched)
    {
        auto supportedMessageTypes = epInfo.getSupportedMessageTypes();
        for (const auto& epMsgType : supportedMessageTypes)
        {
            if (static_cast<MessageType>(epMsgType) == config.type)
            {
                if (config.type == MessageType::vdpci &&
                    config.vendorMessageType.has_value())
                {
                    auto vdmTypes = epInfo.getVDMTypes();
                    messageTypeMatched =
                        std::find(vdmTypes.begin(), vdmTypes.end(),
                                  config.vendorMessageType->cmdSetType()) !=
                        vdmTypes.end();
                }
                else
                {
                    messageTypeMatched = true;
                }
                break;
            }
        }
    }
    if (!messageTypeMatched)
    {
        return false;
    }
    if (config.bindingType == BindingType::mctpOverAny ||
        estimateBindingType(epInfo.devID.networkId()) == config.bindingType)
    {
        return true;
    }
    return false;
}

/* TODO: Use busctl introspect au.com.codeconstruct.MCTP1
 * /au/com/codeconstruct/mctp1/networks/1 au.com.codeconstruct.MCTP.Network1 to
 * identify local endpoint
 */
bool MCTPImpl::isOwnEid(DeviceID devID)
{
    auto localEndpoints = readPropertyValue<std::vector<unsigned char>>(
        *connection, ccMctpService,
        std::string("/au/com/codeconstruct/mctp1/networks/") +
            std::to_string(devID.networkId()),
        "au.com.codeconstruct.MCTP.Network1", "LocalEIDs");
    return std::find_if(localEndpoints.begin(), localEndpoints.end(),
                        [devID](unsigned char endpointId) {
                            return endpointId == devID.mctpEID();
                        }) != localEndpoints.end();
}

// Detect endpoints APIs do nothing. All detection logic is taken care at point
// of construction. new endpoints are asyncronously detected via signals. This
// function is kept to maintain backward compatibility.
void MCTPImpl::detectMctpEndpointsAsync(StatusCallback&& registerCB)
{
    boost::asio::post(connection->get_io_context(), [this, registerCB]() {
        registerCB(
            boost::system::errc::make_error_code(boost::system::errc::success),
            this);
    });
}

// This function is kept to maintain backward compatibility.
void MCTPImpl::triggerMCTPDeviceDiscovery(const DeviceID devID)
{
    // Trigger does nothing
    (void)(devID);
}

int MCTPImpl::reserveBandwidth(boost::asio::yield_context yield,
                               const DeviceID devID, const uint16_t timeout)
{
    // TODO: skip for now
    return -1;
}

int MCTPImpl::releaseBandwidth(boost::asio::yield_context yield,
                               const DeviceID devID)
{
    // TODO: skip for now
    return -1;
}

/* collects all information related to endpoint and update allEndpoints.
 */
void MCTPImpl::setupEndpoints(std::optional<boost::asio::yield_context> yield)
{
    boost::system::error_code ec;
    DictType<
        sdbusplus::message::object_path,
        DictType<std::string, DictType<std::string, MctpPropertiesVariantType>>>
        values;
    try
    {
        auto getManagedObjects = mctpw::methodCall<decltype(values)>(
            *connection, ccMctpService, "/au/com/codeconstruct/mctp1",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", yield);
        if (getManagedObjects)
        {
            values = getManagedObjects.value();
        }
        else
        {
            throw std::runtime_error("No managed objects read from " +
                                     ccMctpService + " Bus ");
        }
        for (const auto& [objectPath, dbusInterfaces] : values)
        {
            if (objectPath.str.starts_with(
                    "/au/com/codeconstruct/mctp1/networks"))
            {
                handleEndpointAddition(objectPath.str, dbusInterfaces);
            }
            else if (objectPath.str.starts_with(
                         "/au/com/codeconstruct/mctp1/interfaces"))
            {
                handleIfaceAddition(objectPath.str, dbusInterfaces);
            }
            else
            {
                phosphor::logging::log<phosphor::logging::level::INFO>(
                    (std::string("Interface ") + objectPath.str +
                     " addition not handelled")
                        .c_str());
            }
        }
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::WARNING>(
            (std::string("Error getting managed objects on ") + ccMctpService +
             " Bus ")
                .c_str());
    }
    try
    {
        auto getManagedObjects = mctpw::methodCall<decltype(values)>(
            *connection, spdmService, "/com/intel/spdmd_secure_session",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", yield);
        if (getManagedObjects)
        {
            values = getManagedObjects.value();
        }
        else
        {
            throw std::runtime_error("No managed objects read from " +
                                     spdmService + " Bus ");
        }

        for (const auto& [objectPath, dbusInterfaces] : values)
        {
            if (objectPath.str.starts_with(
                    "/com/intel/spdmd_secure_session/networks"))
            {
                handleSPDMEndpointAddition(objectPath.str, dbusInterfaces);
            }
        }
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::WARNING>(
            (std::string("Error getting managed objects on ") + spdmService +
             " Bus ")
                .c_str());
    }
}

static sockaddr_mctp createMCTPSockAddr(DeviceID devID, MessageType msgType)
{
    struct sockaddr_mctp addr{0};
    addr.smctp_family = AF_MCTP;
    addr.smctp_network = devID.networkId();
    addr.smctp_addr.s_addr = devID.mctpEID();
    addr.smctp_type = static_cast<uint8_t>(msgType);
    addr.smctp_tag = MCTP_TAG_OWNER;
    return addr;
}

void MCTPImpl::sendReceiveAsync(ReceiveCallback callback, DeviceID devID,
                                const ByteArray& request,
                                std::chrono::milliseconds timeout)
{
    if (allEndpoints.contains(devID) && allEndpoints.at(devID).routeViaSPDM())
    {
        connection->async_method_call(
            [callback, msgType = request[0]](boost::system::error_code ec,
                                             ByteArray& recvData) {
                ByteArray resp;
                resp.push_back(msgType);
                resp.insert(resp.end(), recvData.begin(), recvData.end());
                callback(ec, resp);
            },
            spdmService, "/xyz/openbmc_project/mctp",
            "xyz.openbmc_project.mctp", "SendReceiveMessage", devID.mctpEID(),
            static_cast<int32_t>(devID.networkId()), request[0],
            ByteArray(request.begin() + 1, request.end()),
            static_cast<uint16_t>(timeout.count()));
        return;
    }

    sockaddr_mctp addr =
        createMCTPSockAddr(devID, static_cast<MessageType>(request[0]));
    datagram::endpoint sendEndPoint{&addr, sizeof(addr)};
    auto sock = std::make_shared<datagram::socket>(connection->get_io_context(),
                                                   datagram{AF_MCTP, 0});

    auto task = [sock, callback, this, timeout](boost::system::error_code ec,
                                                int status) {
        if (ec)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                (std::string("sendReceiveAsync: send failed: ") + ec.message() +
                 " (errno: " + std::to_string(errno) + ")")
                    .c_str());
            ByteArray resp;
            callback(ec, resp);
            return;
        }
        auto waitTimer = std::make_shared<boost::asio::steady_timer>(
            connection->get_io_context());
        waitTimer->expires_after(timeout);
        waitTimer->async_wait([sock](const boost::system::error_code& ec) {
            if (ec)
            {
                return;
            }
            sock->cancel();
        });
        sock->async_wait(
            datagram::socket::wait_read,
            [sock, callback, waitTimer, this](boost::system::error_code ec) {
                if (ec)
                {
                    if (ec == boost::asio::error::operation_aborted)
                    {
                        // Socket was cancelled due to timeout
                        phosphor::logging::log<phosphor::logging::level::ERR>(
                            "sendReceiveAsync: Operation timed out");
                        ByteArray resp;
                        callback(boost::system::errc::make_error_code(
                                     boost::system::errc::timed_out),
                                 resp);
                    }
                    else
                    {
                        phosphor::logging::log<phosphor::logging::level::ERR>(
                            (std::string("sendReceiveAsync: receive failed: ") +
                             ec.message() +
                             " (errno: " + std::to_string(errno) + ")")
                                .c_str());
                        ByteArray resp;
                        callback(ec, resp);
                    }
                    return;
                }
                int sd = sock->native_handle();
                int readLen =
                    recvfrom(sd, NULL, 0, MSG_PEEK | MSG_TRUNC, NULL, 0);
                std::vector<uint8_t> recvData(readLen + 1);
                struct sockaddr_mctp addr{0};
                datagram::endpoint recvEndPoint{&addr, sizeof(addr)};
                std::size_t recvSize = sock->receive_from(
                    boost::asio::mutable_buffer(recvData.data() + 1,
                                                recvData.size() - 1),
                    recvEndPoint);
                recvData[0] = addr.smctp_type;
                callback(boost::system::errc::make_error_code(
                             boost::system::errc::success),
                         recvData);
            });
    };
    sock->async_send_to(
        boost::asio::const_buffer(request.data() + 1, request.size() - 1),
        sendEndPoint, task);
    // sock will be kept in scope by lambda capture
}

std::pair<boost::system::error_code, ByteArray>
    MCTPImpl::sendReceiveYield(boost::asio::yield_context yield, DeviceID devID,
                               const ByteArray& request,
                               std::chrono::milliseconds timeout)
{
    struct HeapData
    {
        std::pair<boost::system::error_code, ByteArray> receiveResult;
        sockaddr_mctp addr;
        std::shared_ptr<datagram::socket> sock;
        ByteArray request;
        std::shared_ptr<boost::asio::steady_timer> waitTimer;
    };

    auto heapData = std::make_shared<HeapData>();
    heapData->request = request;

    if (allEndpoints.contains(devID) && allEndpoints.at(devID).routeViaSPDM())
    {
        auto recvData = connection->yield_method_call<ByteArray>(
            yield, heapData->receiveResult.first, spdmService,
            "/xyz/openbmc_project/mctp", "xyz.openbmc_project.mctp",
            "SendReceiveMessage", devID.mctpEID(),
            static_cast<int32_t>(devID.networkId()), request[0],
            ByteArray(request.begin() + 1, request.end()),
            static_cast<uint16_t>(timeout.count()));
        heapData->receiveResult.second.push_back(request[0]);
        heapData->receiveResult.second.insert(
            heapData->receiveResult.second.end(), recvData.begin(),
            recvData.end());
        return heapData->receiveResult;
    }
    heapData->sock = std::make_shared<datagram::socket>(
        connection->get_io_context(), datagram{AF_MCTP, 0});

    sockaddr_mctp addr =
        createMCTPSockAddr(devID, static_cast<MessageType>(request[0]));
    heapData->addr = addr;
    datagram::endpoint sendEndPoint{&addr, sizeof(addr)};

    auto sendCount = heapData->sock->async_send_to(
        boost::asio::const_buffer(request.data() + 1, request.size() - 1),
        sendEndPoint, yield[heapData->receiveResult.first]);
    if (sendCount != (request.size() - 1))
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("sendReceiveYield: send failed " + std::to_string(sendCount))
                .c_str());
        return heapData->receiveResult;
    }

    heapData->waitTimer = std::make_shared<boost::asio::steady_timer>(
        connection->get_io_context());
    heapData->waitTimer->expires_after(timeout);
    boost::asio::posix::stream_descriptor sd(
        connection->get_io_context(), ::dup(heapData->sock->native_handle()));

    auto task = [this, heapData](const boost::system::error_code& ec) {
        if (ec)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                (std::string("sendReceiveYield: Error while socket wait: ") +
                 ec.message() + " (errno: " + std::to_string(errno) + ")")
                    .c_str());
            heapData->receiveResult.first = ec;
            heapData->waitTimer->cancel();
            return;
        }
        // No need to use async_read(yield[]) APIs because data is ready to read
        int readLen = recvfrom(heapData->sock->native_handle(), NULL, 0,
                               MSG_PEEK | MSG_TRUNC, NULL, 0);
        if (readLen < 0)
        {
            boost::system::error_code receiveEc =
                boost::system::errc::make_error_code(
                    static_cast<boost::system::errc::errc_t>(errno));
            phosphor::logging::log<phosphor::logging::level::ERR>(
                (std::string("sendReceiveYield: recvfrom() failed: ") +
                 receiveEc.message() + " (errno: " + std::to_string(errno) +
                 ")")
                    .c_str());
            heapData->receiveResult.first = receiveEc;
            heapData->waitTimer->cancel();
            return;
        }
        std::vector<uint8_t> recvData(readLen);
        sockaddr_mctp addr{0};
        datagram::endpoint recvEndPoint{&addr, sizeof(addr)};
        std::size_t recvSize = heapData->sock->receive_from(
            boost::asio::mutable_buffer(recvData.data(), recvData.size()),
            recvEndPoint);
        heapData->receiveResult.second.push_back(heapData->request[0]);
        heapData->receiveResult.second.insert(
            heapData->receiveResult.second.end(), recvData.begin(),
            recvData.begin() + recvSize);
        heapData->receiveResult.first =
            boost::system::errc::make_error_code(boost::system::errc::success);
        heapData->waitTimer->cancel();
    };

    sd.async_wait(boost::asio::posix::stream_descriptor::wait_read, task);
    boost::system::error_code ec;
    heapData->waitTimer->async_wait(yield[ec]);
    if (!ec)
    {
        heapData->receiveResult.first = boost::system::errc::make_error_code(
            boost::system::errc::timed_out);
    }
    return heapData->receiveResult;
}

boost::system::error_code
    MCTPImpl::registerResponder(const VersionFields& version)
{
    std::vector<VersionFields> versions = {version};
    return registerResponder(versions);
}

boost::system::error_code MCTPImpl::registerResponder(
    const std::vector<VersionFields>& responderVersions)
{
    if (responderVersions.empty())
    {
        return boost::system::errc::make_error_code(
            boost::system::errc::io_error);
    }

    static const std::string registerTypeSupport = "RegisterTypeSupport";
    static const std::string registerVDMTypeSupport = "RegisterVDMTypeSupport";
    std::string methodName = registerTypeSupport;
    bool isVDM =
        config.type == MessageType::vdpci || config.type == MessageType::vdiana;
    if (isVDM)
    {
        if (!config.vendorMessageType.has_value() ||
            !config.vendorId.has_value())
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Vendor message type not set for VDM registration");
            return boost::system::errc::make_error_code(
                boost::system::errc::io_error);
        }
        methodName = registerVDMTypeSupport;
    }

    auto msg = connection->new_method_call(
        "au.com.codeconstruct.MCTP1", "/au/com/codeconstruct/mctp1",
        "au.com.codeconstruct.MCTP1", methodName.c_str());

    if (isVDM)
    {
        uint8_t vidFormat = config.type == MessageType::vdpci ? 0x00 : 0x01;
        std::variant<uint16_t> vendorId = config.vendorId.value_or(0x8086);
        uint16_t cmdSet = config.vendorMessageType.value().cmdSetType();
        phosphor::logging::log<phosphor::logging::level::INFO>(
            (std::string("Registering VDM responder with vid format ") +
             std::to_string(vidFormat) + " vendorId " +
             std::to_string(std::get<uint16_t>(vendorId)) + " cmdSet " +
             std::to_string(cmdSet))
                .c_str());
        msg.append(vidFormat, vendorId, cmdSet);
    }
    else
    {
        msg.append(static_cast<uint8_t>(config.type));

        std::vector<uint32_t> versionArr(responderVersions.size());
        for (std::size_t i = 0; i < responderVersions.size(); i++)
        {
            VersionFields version = responderVersions[i];
            uint32_t* ptr = reinterpret_cast<uint32_t*>(&version);
            versionArr[i] = *ptr;
        }
        msg.append(versionArr);
    }

    try
    {
        connection->call(msg);
    }
    catch (const std::exception& e)
    {
        std::string warnMsg =
            std::string("Unable to register responder. Error") + e.what();
        phosphor::logging::log<phosphor::logging::level::ERR>(warnMsg.c_str());
        return boost::system::errc::make_error_code(
            boost::system::errc::io_error);
    }
    return boost::system::errc::make_error_code(boost::system::errc::success);
}

std::pair<boost::system::error_code, ByteArray>
    MCTPImpl::sendReceiveBlocked(DeviceID devID, const ByteArray& request,
                                 std::chrono::milliseconds timeout)
{
    auto receiveResult = std::make_pair(
        boost::system::errc::make_error_code(boost::system::errc::io_error),
        ByteArray());
    if (allEndpoints.contains(devID) && allEndpoints.at(devID).routeViaSPDM())
    {
        try
        {
            ByteArray recvData = mctpw::methodCall<decltype(recvData)>(
                *connection, spdmService, "/xyz/openbmc_project/mctp",
                "xyz.openbmc_project.mctp", "SendReceiveMessage",
                devID.mctpEID(), static_cast<int32_t>(devID.networkId()),
                request[0], ByteArray(request.begin() + 1, request.end()),
                static_cast<uint16_t>(timeout.count()));
            if (recvData.size() > 0)
            {
                receiveResult.second.push_back(request[0]);
                receiveResult.second.insert(receiveResult.second.end(),
                                            recvData.begin(), recvData.end());
                receiveResult.first = boost::system::errc::make_error_code(
                    boost::system::errc::success);
            }
            return receiveResult;
        }
        catch (const std::exception& e)
        {
            std::string warnMsg =
                std::string("sendReceiveBlocked exception: ") + e.what();
            phosphor::logging::log<phosphor::logging::level::DEBUG>(
                warnMsg.c_str());
            return receiveResult;
        }
    }

    boost::system::error_code ec;
    socklen_t addrlen;
    struct sockaddr_mctp addr{0};
    addr.smctp_family = AF_MCTP;
    addr.smctp_network = devID.networkId();
    addr.smctp_addr.s_addr = devID.mctpEID();
    addr.smctp_type = request[0];
    addr.smctp_tag = MCTP_TAG_OWNER;
    try
    {
        int socketFD = socket(AF_MCTP, SOCK_DGRAM, 0);
        if (socketFD < 0)
        {
            int socketErrno = errno;
            std::string errorMsg =
                "sendReceiveBlocked: Failed to create socket: " +
                std::string(strerror(socketErrno)) +
                " (errno: " + std::to_string(socketErrno) + ")";
            phosphor::logging::log<phosphor::logging::level::ERR>(
                errorMsg.c_str());
            receiveResult.first = boost::system::errc::make_error_code(
                static_cast<boost::system::errc::errc_t>(socketErrno));
            return receiveResult;
        }
        ScopedFD scopedSocketFD(socketFD);

        // Convert std::chrono::milliseconds to struct timeval
        struct timeval tv;
        auto timeoutSec =
            std::chrono::duration_cast<std::chrono::seconds>(timeout);
        auto timeoutUsec =
            std::chrono::duration_cast<std::chrono::microseconds>(timeout -
                                                                  timeoutSec);

        tv.tv_sec = timeoutSec.count();
        tv.tv_usec = timeoutUsec.count();

        // Use select() for timeout handling
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socketFD, &readfds);

        ssize_t rc =
            sendto(socketFD, reinterpret_cast<const void*>(request.data() + 1),
                   request.size() - 1, 0,
                   reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        if (rc != (request.size() - 1))
        {
            int sendErrno = errno;
            std::string errorMsg =
                "sendReceiveBlocked: sendto sent incomplete data: " +
                std::string(strerror(sendErrno)) +
                " (errno: " + std::to_string(sendErrno) +
                ", sent: " + std::to_string(rc) +
                ", expected: " + std::to_string(request.size() - 1) + ")";
            phosphor::logging::log<phosphor::logging::level::ERR>(
                errorMsg.c_str());
            receiveResult.first = boost::system::errc::make_error_code(
                static_cast<boost::system::errc::errc_t>(sendErrno));
            return receiveResult;
        }

        int selectResult = select(socketFD + 1, &readfds, NULL, NULL, &tv);
        if (selectResult < 0)
        {
            int selectErrno = errno;
            std::string errorMsg = "sendReceiveBlocked: select() failed: " +
                                   std::string(strerror(selectErrno)) +
                                   " (errno: " + std::to_string(selectErrno) +
                                   ")";
            phosphor::logging::log<phosphor::logging::level::ERR>(
                errorMsg.c_str());
            receiveResult.first = boost::system::errc::make_error_code(
                static_cast<boost::system::errc::errc_t>(selectErrno));
            return receiveResult;
        }
        else if (selectResult == 0)
        {
            phosphor::logging::log<phosphor::logging::level::DEBUG>(
                "sendReceiveBlocked: receive timed out");
            receiveResult.first = boost::system::errc::make_error_code(
                boost::system::errc::timed_out);
            return receiveResult;
        }

        int readLen =
            recvfrom(socketFD, NULL, 0, MSG_PEEK | MSG_TRUNC, NULL, 0);
        if (readLen <= 0)
        {
            int recvErrno = errno;
            std::string errorMsg =
                "sendReceiveBlocked: Failed to determine read length: " +
                std::string(strerror(recvErrno)) +
                " (errno: " + std::to_string(recvErrno) +
                ", readLen: " + std::to_string(readLen) + ")";
            phosphor::logging::log<phosphor::logging::level::ERR>(
                errorMsg.c_str());
            receiveResult.first = boost::system::errc::make_error_code(
                static_cast<boost::system::errc::errc_t>(recvErrno));
            return receiveResult;
        }

        std::vector<uint8_t> recvData(readLen);
        rc = recvfrom(socketFD, reinterpret_cast<void*>(recvData.data()),
                      recvData.size(), MSG_TRUNC,
                      reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
        if (rc < 0)
        {
            int recvErrno = errno;
            std::string errorMsg = "sendReceiveBlocked:recvfrom failed: " +
                                   std::string(strerror(recvErrno)) +
                                   " (errno: " + std::to_string(recvErrno) +
                                   ", readLen: " + std::to_string(readLen) +
                                   ", rc: " + std::to_string(rc) + ")";
            phosphor::logging::log<phosphor::logging::level::ERR>(
                errorMsg.c_str());
            receiveResult.first = boost::system::errc::make_error_code(
                static_cast<boost::system::errc::errc_t>(recvErrno));
            return receiveResult;
        }

        receiveResult.first =
            boost::system::errc::make_error_code(boost::system::errc::success);
        receiveResult.second.push_back(request[0]);
        receiveResult.second.insert(receiveResult.second.end(),
                                    recvData.begin(), recvData.begin() + rc);
        return receiveResult;
    }
    catch (const std::exception& e)
    {
        std::string warnMsg =
            std::string("sendReceiveBlocked exception: ") + e.what();
        phosphor::logging::log<phosphor::logging::level::ERR>(warnMsg.c_str());
        // For unexpected exceptions, use io_error as fallback
        receiveResult.first =
            boost::system::errc::make_error_code(boost::system::errc::io_error);
        return receiveResult;
    }
}

void MCTPImpl::sendAsync(const SendCallback& callback, const DeviceID devID,
                         const uint8_t msgTag, const bool tagOwner,
                         const ByteArray& request)
{
    if (allEndpoints.contains(devID) && allEndpoints.at(devID).routeViaSPDM())
    {
        connection->async_method_call(
            [callback](boost::system::error_code ec, int rc) {
                callback(ec, rc + 1);
            },
            spdmService, "/xyz/openbmc_project/mctp",
            "xyz.openbmc_project.mctp", "SendMessage", devID.mctpEID(),
            static_cast<int32_t>(devID.networkId()), msgTag, request[0],
            ByteArray(request.begin() + 1, request.end()));
        return;
    }

    sockaddr_mctp addr =
        createMCTPSockAddr(devID, static_cast<MessageType>(request[0]));
    addr.smctp_tag = tagOwner ? MCTP_TAG_OWNER : (msgTag & MCTP_TAG_MASK);

    datagram::endpoint sendEndPoint{&addr, sizeof(addr)};
    auto sock = std::make_shared<datagram::socket>(connection->get_io_context(),
                                                   datagram{AF_MCTP, 0});

    auto reqCopy = std::make_shared<ByteArray>(request);
    auto newCB = [reqCopy, callback, sock](boost::system::error_code ec,
                                           std::size_t bytesSent) {
        int status = -1;
        if (ec)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                (std::string("sendAsync: Send failed: ") + ec.message())
                    .c_str());
        }
        if (bytesSent != (reqCopy->size() - 1))
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                (std::string("sendAsync: Send incomplete: ") +
                 std::to_string(bytesSent) + " expected " +
                 std::to_string(reqCopy->size()))
                    .c_str());
        }
        if (!ec && (bytesSent == (reqCopy->size() - 1)))
        {
            status = 0;
        }

        callback(ec, status);
    };
    sock->async_send_to(
        boost::asio::const_buffer(reqCopy->data() + 1, reqCopy->size() - 1),
        sendEndPoint, newCB);
}

std::pair<boost::system::error_code, int>
    MCTPImpl::sendYield(boost::asio::yield_context& yield, const DeviceID devID,
                        const uint8_t msgTag, const bool tagOwner,
                        const ByteArray& request)
{
    if (allEndpoints.contains(devID) && allEndpoints.at(devID).routeViaSPDM())
    {
        boost::system::error_code ec =
            boost::system::errc::make_error_code(boost::system::errc::success);
        int status = connection->yield_method_call<int>(
            yield, ec, spdmService, "/xyz/openbmc_project/mctp",
            "xyz.openbmc_project.mctp", "SendMessage", devID.mctpEID(),
            static_cast<int32_t>(devID.networkId()), msgTag, request[0],
            ByteArray(request.begin() + 1, request.end()));
        return std::make_pair(ec, status);
    }

    try
    {
        sockaddr_mctp addr =
            createMCTPSockAddr(devID, static_cast<MessageType>(request[0]));
        addr.smctp_tag = tagOwner ? MCTP_TAG_OWNER : (msgTag & MCTP_TAG_MASK);
        datagram::endpoint sendEndPoint{&addr, sizeof(addr)};
        datagram::socket sock(connection->get_io_context(),
                              datagram{AF_MCTP, 0});
        boost::system::error_code ec;
        auto bytesSent = sock.async_send_to(
            boost::asio::const_buffer(request.data() + 1, request.size() - 1),
            sendEndPoint, yield[ec]);
        int status = 0;
        if (ec)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                (std::string("sendYield: Send failed: ") + ec.message())
                    .c_str());
            status = -1;
        }
        else if (bytesSent != (request.size() - 1))
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                (std::string("sendYield: Send incomplete: sent ") +
                 std::to_string(bytesSent) + ", expected " +
                 std::to_string(request.size() - 1) +
                 " (errno: " + std::to_string(errno) + ")")
                    .c_str());
            status = -1;
            ec = boost::system::errc::make_error_code(
                boost::system::errc::io_error);
        }
        return std::make_pair(ec, status);
    }
    catch (const std::exception& e)
    {
        std::string warnMsg = std::string("sendYield exception: ") + e.what();
        phosphor::logging::log<phosphor::logging::level::ERR>(warnMsg.c_str());
        return std::make_pair(
            boost::system::errc::make_error_code(boost::system::errc::io_error),
            -1);
    }
}

std::pair<boost::system::error_code, int>
    MCTPImpl::sendBlocked(const DeviceID devID, const uint8_t msgTag,
                          const bool tagOwner, const ByteArray& request)
{
    if (allEndpoints.contains(devID) && allEndpoints.at(devID).routeViaSPDM())
    {
        try
        {
            int status = mctpw::methodCall<int>(
                *connection, spdmService, "/xyz/openbmc_project/mctp",
                "xyz.openbmc_project.mctp", "SendMessage", devID.mctpEID(),
                static_cast<int32_t>(devID.networkId()), msgTag, request[0],
                ByteArray(request.begin() + 1, request.end()));
            return std::make_pair(boost::system::errc::make_error_code(
                                      boost::system::errc::success),
                                  status);
        }
        catch (const sdbusplus::exception::SdBusError& sdbusError)
        {
            phosphor::logging::log<phosphor::logging::level::DEBUG>(
                "SendBlocked: Error in method call ",
                phosphor::logging::entry("EID=%d", devID.id));
            return std::make_pair(boost::system::errc::make_error_code(
                                      boost::system::errc::io_error),
                                  -1);
        }
    }

    struct sockaddr_mctp addr =
        createMCTPSockAddr(devID, static_cast<MessageType>(request[0]));
    addr.smctp_tag = tagOwner ? MCTP_TAG_OWNER : (msgTag & MCTP_TAG_MASK);

    try
    {
        int socketFD = socket(AF_MCTP, SOCK_DGRAM, 0);
        if (socketFD < 0)
        {
            int socketErrno = errno;
            std::string errorMsg = "sendBlocked: Failed to create socket: " +
                                   std::string(strerror(socketErrno)) +
                                   " (errno: " + std::to_string(socketErrno) +
                                   ")";
            phosphor::logging::log<phosphor::logging::level::ERR>(
                errorMsg.c_str());
            return std::make_pair(
                boost::system::errc::make_error_code(
                    static_cast<boost::system::errc::errc_t>(socketErrno)),
                -1);
        }
        ScopedFD scopedSocketFD(socketFD);

        ssize_t rc =
            sendto(socketFD, reinterpret_cast<const void*>(request.data() + 1),
                   request.size() - 1, 0,
                   reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        if (rc != (request.size() - 1))
        {
            int sendErrno = errno;
            std::string errorMsg =
                "sendBlocked: sendto sent incomplete data: " +
                std::string(strerror(sendErrno)) +
                " (errno: " + std::to_string(sendErrno) +
                ", sent: " + std::to_string(rc) +
                ", expected: " + std::to_string(request.size() - 1) + ")";
            phosphor::logging::log<phosphor::logging::level::ERR>(
                errorMsg.c_str());
            return std::make_pair(
                boost::system::errc::make_error_code(
                    static_cast<boost::system::errc::errc_t>(sendErrno)),
                -1);
        }
    }
    catch (const std::exception& e)
    {
        std::string warnMsg = std::string("sendBlocked exception: ") + e.what();
        phosphor::logging::log<phosphor::logging::level::ERR>(warnMsg.c_str());
        return std::make_pair(
            boost::system::errc::make_error_code(boost::system::errc::io_error),
            -1);
    }

    return std::make_pair(
        boost::system::errc::make_error_code(boost::system::errc::success), 0);
}

std::optional<std::string>
    MCTPImpl::getDeviceLocation(const DeviceID extendedEID)
{
    // Implementation is deferred
    return std::string("Unknown");
}

void MCTPImpl::getOwnEIDs(OwnEIDChangeCallback callback)
{
    if (!callback)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "GetOwnEIDs callback is empty");
        return;
    }

    this->eidChangeCallback = callback;
    for (const auto& [devID, epInfo] : allEndpoints)
    {
        if (epInfo.selfEndpoint)
        {
            boost::asio::post(connection->get_io_context(), [this, devID]() {
                if (this->eidChangeCallback)
                {
                    this->eidChangeCallback(devID);
                }
            });
        }
    }
}

void MCTPImpl::setupDbusListener()
{
    static const std::string ccMctprule =
        "type='signal',path='/au/com/codeconstruct/mctp1'";
    this->mctpChangesWatch = std::make_unique<sdbusplus::bus::match::match>(
        *connection, ccMctprule,
        std::bind(&MCTPImpl::onMCTPEvent, this, std::placeholders::_1));

    static const std::string kMctprule =
        "type='signal',path='/xyz/openbmc_project/mctp'";
    this->vdmMessagesWatch = std::make_unique<sdbusplus::bus::match::match>(
        *connection, kMctprule,
        std::bind(&MCTPImpl::onMCTPEvent, this, std::placeholders::_1));

    static const std::string spdmRule =
        "type='signal',sender='com.intel.spdmd.secure.session',"
        "path_namespace='/com/intel/spdmd_secure_session'";
    this->spdmWatch = std::make_unique<sdbusplus::bus::match::match>(
        *connection, spdmRule,
        std::bind(&MCTPImpl::onMCTPEvent, this, std::placeholders::_1));

    phosphor::logging::log<phosphor::logging::level::INFO>(
        "Wrapper: Listening for all MCTP related signals");
}

void MCTPImpl::onNewInterface(sdbusplus::message::message& msg)
{
    DictType<std::string, DictType<std::string, MctpPropertiesVariantType>>
        dbusInterfaces;
    sdbusplus::message::object_path objectPath;
    msg.read(objectPath, dbusInterfaces);
    phosphor::logging::log<phosphor::logging::level::INFO>(
        (std::string("Interface added on ") + objectPath.str).c_str());

    if (objectPath.str.starts_with("/au/com/codeconstruct/mctp1/networks"))
    {
        handleEndpointAddition(objectPath.str, dbusInterfaces);
    }
    else if (objectPath.str.starts_with(
                 "/au/com/codeconstruct/mctp1/interfaces"))
    {
        handleIfaceAddition(objectPath.str, dbusInterfaces);
    }
    else if (objectPath.str.starts_with(
                 "/com/intel/spdmd_secure_session/networks"))
    {
        handleSPDMEndpointAddition(objectPath.str, dbusInterfaces);
    }
    else
    {
        phosphor::logging::log<phosphor::logging::level::INFO>(
            (std::string("Interface ") + objectPath.str +
             " addition not handelled")
                .c_str());
    }
}

void MCTPImpl::handleIfaceAddition(
    const std::string objectPath,
    const DictType<std::string,
                   DictType<std::string, MctpPropertiesVariantType>>& values)
{
    std::string interfaces("interfaces");
    auto ifaceNameBegin = objectPath.begin() + objectPath.find(interfaces) +
                          interfaces.size() + 1;
    if (ifaceNameBegin >= objectPath.end())
    {
        return;
    }
    std::string ifaceName(ifaceNameBegin, objectPath.end());
    auto ifaceInfoInterfaceItr =
        values.find("au.com.codeconstruct.MCTP.Interface1");
    if (ifaceInfoInterfaceItr == values.end())
    {
        return;
    }
    auto allProperties = ifaceInfoInterfaceItr->second;
    auto networkIDItr = allProperties.find("NetworkId");
    if (networkIDItr == allProperties.end())
    {
        return;
    }
    NetworkID networkID = std::get<uint32_t>(networkIDItr->second);
    ifaceNetworkMap[ifaceName] = networkID;
}

void MCTPImpl::handleIfaceRemoval(
    const std::string objectPath,
    const std::vector<std::string>& dbusInterfaces)
{
    (void)(dbusInterfaces);
    std::string interfaces("interfaces");
    auto ifaceNameBegin = objectPath.begin() + objectPath.find(interfaces) +
                          interfaces.size() + 1;
    if (ifaceNameBegin >= objectPath.end())
    {
        return;
    }
    std::string ifaceName(ifaceNameBegin, objectPath.end());
    ifaceNetworkMap.erase(ifaceName);
}

void MCTPImpl::handleSPDMEndpointAddition(
    const std::string objectPath,
    const DictType<std::string,
                   DictType<std::string, MctpPropertiesVariantType>>& values)
{
    auto optDevID = extractDeviceIDFromObjectPath(objectPath);
    if (!optDevID.has_value())
    {
        phosphor::logging::log<phosphor::logging::level::INFO>(
            (std::string("Failed to extract SPDM endpoint information from  ") +
             objectPath)
                .c_str());
        return;
    }
    DeviceID devID(optDevID.value());
    if (!allEndpoints.contains(devID))
    {
        std::stringstream ss;
        ss << "Ignoring " << objectPath << " since " << devID
           << " is not yet signalled by cc mctpd";
        phosphor::logging::log<phosphor::logging::level::WARNING>(
            ss.str().c_str());
        return;
    }
    auto interface = values.find("com.intel.spdmd_secure_session.spdm_device");
    if (interface == values.end())
    {
        phosphor::logging::log<phosphor::logging::level::INFO>(
            (std::string("Failed to find secure session interface on path :") +
             objectPath)
                .c_str());
        return;
    }
    auto allProperties = interface->second;
    auto sessionItr = allProperties.find("SPDMSessionEstablished");
    if (sessionItr == allProperties.end())
    {
        return;
    }
    if (std::get<bool>(sessionItr->second))
    {
        allEndpoints.at(devID).enableSPDMRoute();
    }
    else
    {
        allEndpoints.at(devID).disableSPDMRoute();
    }
}
void MCTPImpl::handleSPDMEndpointRemoval(
    const std::string objectPath,
    const std::vector<std::string>& dbusInterfaces)
{
    (void)(dbusInterfaces);
    auto optDevID = extractDeviceIDFromObjectPath(objectPath);
    if (!optDevID.has_value())
    {
        phosphor::logging::log<phosphor::logging::level::INFO>(
            (std::string("Failed to extract SPDM endpoint information from  ") +
             objectPath)
                .c_str());
        return;
    }
    DeviceID devID(optDevID.value());
    if (!allEndpoints.contains(devID))
    {
        std::stringstream ss;
        ss << "Ignoring " << objectPath << " since " << devID
           << " is not yet signalled by cc mctpd";
        phosphor::logging::log<phosphor::logging::level::WARNING>(
            ss.str().c_str());
        return;
    }
    allEndpoints.erase(devID);
}

void MCTPImpl::handleEndpointAddition(
    const std::string objectPath,
    const DictType<std::string,
                   DictType<std::string, MctpPropertiesVariantType>>& values)
{
    auto optDevID = extractDeviceIDFromObjectPath(objectPath);
    if (!optDevID.has_value())
    {
        return;
    }
    DeviceID devID(optDevID.value());
    std::string uuid = "";
    auto uuidInterfaceItr = values.find("xyz.openbmc_project.Common.UUID");
    if (uuidInterfaceItr != values.end())
    {
        auto allProperties = uuidInterfaceItr->second;
        auto uuidItr = allProperties.find("UUID");
        if (uuidItr != allProperties.end())
        {
            uuid = std::get<std::string>(uuidItr->second);
        }
    }
    auto endpointsInterfaceItr =
        values.find("xyz.openbmc_project.MCTP.Endpoint");
    if (endpointsInterfaceItr == values.end())
    {
        return;
    }
    auto allProperties = endpointsInterfaceItr->second;
    auto supportedMessageTypeItr = allProperties.find("SupportedMessageTypes");
    auto vdmTypesItr = allProperties.find("VDMTypes");
    if (supportedMessageTypeItr == allProperties.end() ||
        vdmTypesItr == allProperties.end())
    {
        return;
    }
    auto supportedMessageTypes =
        std::get<std::vector<uint8_t>>(supportedMessageTypeItr->second);
    auto vdmTypes = std::get<std::vector<uint16_t>>(vdmTypesItr->second);
    EndpointInfo epInfo(devID, uuid, supportedMessageTypes, vdmTypes,
                        isOwnEid(devID));
    if (allEndpoints.contains(devID))
    {
        if (allEndpoints.at(devID) == epInfo)
        {
            return;
        }
    }
    allEndpoints.insert(std::make_pair(devID, epInfo));
    if (allEndpoints.at(devID).selfEndpoint)
    {
        if (!this->eidChangeCallback)
        {
            phosphor::logging::log<phosphor::logging::level::DEBUG>(
                "GetOwnEIDs callback is empty while trying to trigger");
            return;
        }
        eid_t eid = devID.mctpEID();
        if (eid == 0)
        {
            return;
        }
        boost::asio::post(connection->get_io_context(),
                          [this, devID]() { this->eidChangeCallback(devID); });
    }
    else
    {
        if (!this->networkChangeCallback)
        {
            phosphor::logging::log<phosphor::logging::level::DEBUG>(
                "ReconfigurationCallback callback is empty");
            return;
        }
        if (eligibleForReconfigurationCallback(epInfo))
        {
            boost::asio::spawn(
                connection->get_io_context(),
                [this, devID](boost::asio::yield_context yield) {
                    Event event;
                    event.deviceId = devID;
                    event.type = mctpw::Event::EventType::deviceAdded;
                    this->networkChangeCallback(this, event, yield);
                },
                {});
        }
    }
}

void MCTPImpl::handleEndpointRemoval(const std::string objectPath,
                                     const std::vector<std::string>& interfaces)
{
    auto optDevID = extractDeviceIDFromObjectPath(objectPath);
    if (!optDevID.has_value())
    {
        return;
    }
    DeviceID devID(optDevID.value());
    if (!allEndpoints.contains(devID))
    {
        return;
    }
    if (eligibleForReconfigurationCallback(allEndpoints.at(devID)))
    {
        boost::asio::spawn(
            connection->get_io_context(),
            [this, devID](boost::asio::yield_context yield) {
                Event event;
                event.deviceId = devID;
                event.type = mctpw::Event::EventType::deviceRemoved;
                if (!this->networkChangeCallback)
                {
                    phosphor::logging::log<phosphor::logging::level::DEBUG>(
                        "ReconfigurationCallback callback is empty");
                    return;
                }
                this->networkChangeCallback(this, event, yield);
            },
            {});
    }
    allEndpoints.erase(devID);
}

void MCTPImpl::onInterfaceRemoved(sdbusplus::message::message& msg)
{
    sdbusplus::message::object_path objectPathMsg;
    std::vector<std::string> dbusInterfaces;
    msg.read(objectPathMsg, dbusInterfaces);
    std::string objectPath = objectPathMsg.str;
    phosphor::logging::log<phosphor::logging::level::DEBUG>(
        (std::string("Interface removed on path ") + objectPath).c_str());

    if (objectPath.starts_with("/au/com/codeconstruct/mctp1/networks/"))
    {
        handleEndpointRemoval(objectPath, dbusInterfaces);
    }
    else if (objectPath.starts_with("/au/com/codeconstruct/mctp1/interfaces/"))
    {
        handleIfaceRemoval(objectPath, dbusInterfaces);
    }
    else if (objectPath.starts_with("/com/intel/spdmd_secure_session/networks"))
    {
        handleSPDMEndpointRemoval(objectPath, dbusInterfaces);
    }
}

void MCTPImpl::onMessageReceived(sdbusplus::message::message& msg)
{
    if (!this->receiveCallback && !this->extReceiveCallback)
    {
        return;
    }

    uint8_t srcEid = 0;
    uint32_t networkId = 0;
    uint8_t msgTag = 0;
    uint8_t messageType = 0;
    std::vector<uint8_t> payload;

    msg.read(srcEid, networkId, msgTag, messageType, payload);

    auto binding = estimateBindingType(networkId);
    if (config.bindingType != BindingType::mctpOverAny)
    {
        if (binding != config.bindingType)
        {
            phosphor::logging::log<phosphor::logging::level::DEBUG>(
                "Binding type mismatch. Ignoring message");
            return;
        }
    }
    if (static_cast<MessageType>(messageType) != config.type)
    {
        return;
    }
    if (static_cast<MessageType>(messageType) == MessageType::vdpci)
    {
        struct VendorHeader
        {
            uint16_t vendorId;
            uint16_t intelVendorMessageId;
        } __attribute__((packed));
        VendorHeader* vendorHdr =
            reinterpret_cast<VendorHeader*>(payload.data());

        if (!config.vendorId || !config.vendorMessageType ||
            (be16toh(vendorHdr->vendorId) != config.vendorId) ||
            ((be16toh(vendorHdr->intelVendorMessageId) &
              config.vendorMessageType->mask) !=
             (config.vendorMessageType->value &
              config.vendorMessageType->mask)))
        {
            return;
        }
    }
    payload.insert(payload.begin(), messageType);
    if (this->receiveCallback)
    {
        this->receiveCallback(this, srcEid, false, msgTag, payload, 0);
    }
    if (this->extReceiveCallback)
    {
        this->extReceiveCallback(this, DeviceID(srcEid, networkId), false,
                                 msgTag, payload, 0);
    }
}

void MCTPImpl::onMCTPEvent(sdbusplus::message::message& msg)
{
    static const std::string intfAdded = "InterfacesAdded";
    static const std::string intfRemoved = "InterfacesRemoved";
    static const std::string vdmReceived = "VDMReceived";
    static const std::string spdmMessageReceived = "MessageReceived";
    static const std::string spdmSessionEvent = "PropertiesChanged";
    try
    {
        std::string sender = msg.get_sender();
        auto member = msg.get_member();
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            (std::string("MCTP general event from ") + sender).c_str());

        if (member == intfAdded)
        {
            this->onNewInterface(msg);
        }
        if (member == intfRemoved)
        {
            this->onInterfaceRemoved(msg);
        }
        if (member == spdmSessionEvent)
        {
            this->onSPDMSessionEvent(msg);
        }
        else if ((member == vdmReceived) || (member == spdmMessageReceived))
        {
            this->onMessageReceived(msg);
        }
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            (std::string("Exception in onMCTPEvent: ") + e.what()).c_str());
    }
}

void MCTPImpl::onSPDMSessionEvent(sdbusplus::message::message& msg)
{
    std::string interfaceName;
    std::map<std::string, MctpPropertiesVariantType> changedProperties;
    std::vector<std::string> invalidatedProperties;

    msg.read(interfaceName, changedProperties, invalidatedProperties);

    std::string objectPath = msg.get_path();

    if (interfaceName != "com.intel.spdmd_secure_session.spdm_device")
    {
        return;
    }

    auto optDevID = extractDeviceIDFromObjectPath(objectPath);
    if (!optDevID.has_value())
    {
        phosphor::logging::log<phosphor::logging::level::INFO>(
            (std::string("Failed to extract SPDM endpoint information from  ") +
             objectPath)
                .c_str());
        return;
    }
    DeviceID devID(optDevID.value());

    if (!allEndpoints.contains(devID))
    {
        std::stringstream ss;
        ss << "Ignoring " << objectPath << " since " << devID
           << " is not yet signalled by cc mctpd";
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            ss.str().c_str());
        return;
    }

    // Check if SPDMSessionEstablished property changed
    auto sessionItr = changedProperties.find("SPDMSessionEstablished");
    if (sessionItr != changedProperties.end())
    {
        bool sessionEstablished = std::get<bool>(sessionItr->second);
        phosphor::logging::log<phosphor::logging::level::INFO>(
            (std::string("SPDM Session state changed for ") + objectPath +
             " to " + (sessionEstablished ? "established" : "terminated"))
                .c_str());

        if (sessionEstablished)
        {
            allEndpoints.at(devID).enableSPDMRoute();
        }
        else
        {
            allEndpoints.at(devID).disableSPDMRoute();
        }
    }
}

void MCTPImpl::setExtendedReceiveCallback(
    ExtendedReceiveMessageCallback callback)
{
    if (this->receiveCallback != nullptr)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "Registering normal and extended callback is not allowed");
        return;
    }
    this->extReceiveCallback = std::move(callback);
}

void MCTPImpl::initiateSPDMHandshake(
    HandshakeCallback initiateHandshakeCallback, DeviceID devID, bool connState)
{
    // void API
}

MCTPImpl::MCTPImpl(std::shared_ptr<sdbusplus::asio::connection> conn,
                   const MCTPConfiguration& configIn,
                   const ReconfigurationCallback& networkChangeCb,
                   const ReceiveMessageCallback& rxCb) :
    connection(conn), config(configIn), networkChangeCallback(networkChangeCb),
    receiveCallback(rxCb)
{
    setupDbusListener();
    setupEndpoints(std::nullopt);
    if (permittedMessageTypes.contains(config.type))
    {
        struct sockaddr_mctp addr{0};
        addr.smctp_family = AF_MCTP;
        addr.smctp_network = MCTP_NET_ANY;
        addr.smctp_addr.s_addr = MCTP_ADDR_ANY;
        addr.smctp_type = static_cast<uint8_t>(config.type);
        addr.smctp_tag = MCTP_TAG_OWNER;
        datagram::endpoint bindEndPoint{&addr, sizeof(addr)};
        bindToEndpoint(bindEndPoint);
    }
    else if (config.type == MessageType::vdpci)
    {
        // 0x7E are already being listened via signal
    }
    else
    {
        std::string warnMsg =
            std::string("No action will taken to handle incoming message for "
                        "message type ") +
            std::to_string(static_cast<int>(config.type));
        phosphor::logging::log<phosphor::logging::level::WARNING>(
            warnMsg.c_str());
    }
}

void MCTPImpl::bindToEndpoint(datagram::endpoint bindEndpoint)
{
    if (boundSockets.contains(bindEndpoint))
    {
        return;
    }
    auto incomingMsgSocket = std::make_shared<datagram::socket>(
        connection->get_io_context(), datagram{AF_MCTP, 0});
    std::string warnMsg =
        std::string("binding all messages on all mctp network of type ") +
        std::to_string(static_cast<int>(config.type)) +
        " to myself. Any attempt by other service to request this message" +
        " type will lead to undefined behaviour";
    phosphor::logging::log<phosphor::logging::level::WARNING>(warnMsg.c_str());
    incomingMsgSocket->bind(bindEndpoint);
    incomingMsgSocket->async_wait(datagram::socket::wait_read,
                                  std::bind(&MCTPImpl::handleIncomingMessage,
                                            this, incomingMsgSocket,
                                            std::placeholders::_1));
    boundSockets[bindEndpoint] = incomingMsgSocket;
}

void MCTPImpl::handleIncomingMessage(
    std::shared_ptr<datagram::socket> incomingMsgSocket,
    const boost::system::error_code& ec)
{
    if (ec == boost::asio::error::operation_aborted)
    {
        return;
    }
    int readLen = recvfrom(incomingMsgSocket->native_handle(), NULL, 0,
                           MSG_PEEK | MSG_TRUNC, NULL, 0);
    if (readLen < 0)
    {
        std::string warnMsg =
            std::string("Failed to determine read length, Assuming 256");
        phosphor::logging::log<phosphor::logging::level::WARNING>(
            warnMsg.c_str());
        readLen = 256;
    }
    std::vector<uint8_t> recvData(readLen);
    struct sockaddr_mctp addr{0};
    datagram::endpoint recvEndPoint{&addr, sizeof(addr)};
    std::size_t recvSize = incomingMsgSocket->receive_from(
        boost::asio::mutable_buffer(recvData.data(), recvData.size()),
        recvEndPoint);
    if (recvSize < readLen)
    {
        recvData.resize(recvSize);
    }
    if (receiveCallback)
    {
        receiveCallback(this, addr.smctp_addr.s_addr, false,
                        addr.smctp_tag & 0x07, recvData, 0);
    }
    if (extReceiveCallback)
    {
        extReceiveCallback(this,
                           DeviceID(addr.smctp_addr.s_addr, addr.smctp_network),
                           false, addr.smctp_tag & 0x07, recvData, 0);
    }
    incomingMsgSocket->async_wait(datagram::socket::wait_read,
                                  std::bind(&MCTPImpl::handleIncomingMessage,
                                            this, incomingMsgSocket,
                                            std::placeholders::_1));
    return;
}

BindingType MCTPImpl::estimateBindingType(uint32_t networkId)
{
    // FIXME: Hard-coded network ID to binding type mapping needs review
    // Get all objects and properties which implement
    // au.com.codeconstruct.MCTP.Interface1. Check for matchinig network id. And
    // check interface name for binding type.

    switch (networkId)
    {
        case 1:
            return BindingType::mctpOverPcieVdm;
        case 2:
            return BindingType::mctpOverI3C;
        case 3:
            return BindingType::mctpOverSmBus;
        default:
            return BindingType::undefined;
    }
}

MCTPWrapper::EndpointMapExtended MCTPImpl::getEndpointMap()
{
    MCTPWrapper::EndpointMapExtended epMap;
    for (const auto& [devID, epInfo] : allEndpoints)
    {
        if (eligibleForReconfigurationCallback(epInfo))
        {
            epMap.insert(devID);
        }
    }
    return epMap;
}

} // namespace mctpw
