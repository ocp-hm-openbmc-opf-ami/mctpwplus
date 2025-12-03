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
#pragma once

#include "mctp_wrapper.hpp"
#include "socket_if.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/container/flat_map.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus/match.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mctpw
{
/// MCTP Endpoint Id
using ByteArray = std::vector<uint8_t>;
const std::string ccMctpService("au.com.codeconstruct.MCTP1");
const std::string kMctpService("");
const std::string spdmService("com.intel.spdmd.secure.session");

/**
 * @brief Wrapper class to access MCTP functionalities
 *
 */

class MCTPImpl
{
  public:
    /**
     * @brief Construct a new MCTPImpl object
     *
     * @param conn shared_ptr to already existing boost asio::connection object
     * @param configIn MCTP configuration to describe message type and vendor
     * specific data if required.
     * @param networkChangeCb Callback to be executed when a network change
     * occurs in the system. For example a new device is inserted or removed etc
     * @param rxCb Callback to be executed when new MCTP message is
     */
    MCTPImpl(std::shared_ptr<sdbusplus::asio::connection> conn,
             const MCTPConfiguration& configIn,
             const ReconfigurationCallback& networkChangeCb,
             const ReceiveMessageCallback& rxCb);

    ~MCTPImpl()
    {
        for (auto [_, sock] : boundSockets)
        {
            sock->cancel();
        }
    }

    using StatusCallback =
        std::function<void(boost::system::error_code, void*)>;
    using EndpointMapExtended = MCTPWrapper::EndpointMapExtended;
    using ReceiveCallback =
        std::function<void(boost::system::error_code, ByteArray&)>;
    using SendCallback = std::function<void(boost::system::error_code, int)>;
    using HandshakeCallback = std::function<void(boost::system::error_code)>;
    using datagram = boost::asio::generic::datagram_protocol;

    std::shared_ptr<sdbusplus::asio::connection> connection;
    mctpw::MCTPConfiguration config{};
    /// Callback to be executed when a network change occurs
    ReconfigurationCallback networkChangeCallback = nullptr;
    /// Callback to be executed when a MCTP message received
    ReceiveMessageCallback receiveCallback = nullptr;
    ExtendedReceiveMessageCallback extReceiveCallback = nullptr;
    OwnEIDChangeCallback eidChangeCallback;

    static const inline std::unordered_map<MessageType, const std::string>
        msgTypeToPropertyName = {{MessageType::pldm, "PLDM"},
                                 {MessageType::ncsi, "NCSI"},
                                 {MessageType::ethernet, "Ethernet"},
                                 {MessageType::nvmeMgmtMsg, "NVMeMgmtMsg"},
                                 {MessageType::spdm, "SPDM"},
                                 {MessageType::securedMsg, "SECUREDMSG"},
                                 {MessageType::cxlFmApi, "CXLFMAPI"},
                                 {MessageType::cxlCci, "CXLCCI"},
                                 {MessageType::vdpci, "VDPCI"},
                                 {MessageType::vdiana, "VDIANA"},
                                 {MessageType::any, "ANY"}};
    /**
     * @brief This method or its yield variant must be called before accessing
     * any send receive functions. It scan and detect all mctp endpoints exposed
     * on dbus.
     *
     * @param callback Callback to be invoked after mctp endpoint detection with
     * status of the operation
     */
    void detectMctpEndpointsAsync(StatusCallback&& callbackc);
    /**
     * @brief Get a reference to internaly maintained EndpointMap
     *
     * @return const EndpointMapExtended&
     */
    EndpointMapExtended getEndpointMap();

    /**
     * @brief Trigger MCTP device discovery
     *
     */
    void triggerMCTPDeviceDiscovery(const DeviceID devID);

    /**
     * @brief Reserve bandwidth for EID
     *
     * @param yield Boost yield_context to use on dbus call
     * @param dstEId Destination MCTP Device ID
     * @param timeout reserve bandwidth timeout
     * @return dbus send method call return value
     */
    int reserveBandwidth(boost::asio::yield_context yield, const DeviceID devID,
                         const uint16_t timeout);

    /**
     * @brief Release bandwidth for EID
     *
     * @param yield Boost yield_context to use on dbus call
     * @param dstEId Destination MCTP Device ID
     * @return dbus send method call return value
     */
    int releaseBandwidth(boost::asio::yield_context yield,
                         const DeviceID devID);

    /**
     * @brief Send request to dstEId and receive response asynchronously in
     * receiveCb
     *
     * @param receiveCb Callback to be executed when response is ready
     * @param dstEId Destination MCTP Device ID
     * @param request MCTP request byte array
     * @param timeout MCTP receive timeout
     */
    void sendReceiveAsync(ReceiveCallback receiveCb, DeviceID devID,
                          const ByteArray& request,
                          std::chrono::milliseconds timeout);

    /**
     * @brief Send request to dstEId and receive response using yield_context
     *
     * @param yield Boost yield_context to use on dbus call
     * @param dstEId Destination MCTP Device ID
     * @param request MCTP request byte array
     * @param timeout MCTP receive timeout
     * @return std::pair<boost::system::error_code, ByteArray> Pair of boost
     * error code and response byte array
     */
    std::pair<boost::system::error_code, ByteArray>
        sendReceiveYield(boost::asio::yield_context yield, DeviceID devID,
                         const ByteArray& request,
                         std::chrono::milliseconds timeout);
    /**
     * @brief Send request to dstEId and receive response using blocked
     * calls     *
     * @param yield Boost yield_context to use on dbus call
     * @param dstEId Destination MCTP Device ID
     * @param request MCTP request byte array
     * @param timeout MCTP receive timeout
     * @return std::pair<boost::system::error_code, ByteArray> Pair of boost
     * error code and response byte array
     */
    std::pair<boost::system::error_code, ByteArray>
        sendReceiveBlocked(DeviceID devID, const ByteArray& request,
                           std::chrono::milliseconds timeout);

    /**
     * @brief Register a responder application with MCTP layer
     * @param version The version supported by the responder. Use if only one
     * version is supported
     * @return boost error code
     */
    boost::system::error_code registerResponder(const VersionFields& version);
    /**
     * @brief Register a responder application with MCTP layer
     * @param versions List of versions supported by the responder. Use if
     * multiple versions are supported
     * @return boost error code
     */
    boost::system::error_code
        registerResponder(const std::vector<VersionFields>& versions);

    /**
     * @brief Send MCTP request to dstEId and receive status of send operation
     * in callback
     *
     * @param callback Callback that will be invoked with status of send
     * operation
     * @param dstEId Destination MCTP Device ID
     * @param msgTag MCTP message tag value
     * @param tagOwner MCTP tag owner bit. Identifies whether the message tag
     * was originated by the endpoint that is the source of the message
     * @param request MCTP request byte array
     */
    void sendAsync(const SendCallback& callback, const DeviceID devID,
                   const uint8_t msgTag, const bool tagOwner,
                   const ByteArray& request);

    /**
     * @brief Initiates handshake between client and SPDM socket server.
     *
     * This function initiates the handshake process between the client and
     * SPDM socket server for a secure connection. Once the server initializes
     * and sets the secure connection , it calls this method and then the client
     * starts listening and proceeds with socket initialization.
     *
     * @param initiateHandshakeCallback The callback function to be invoked for
     * initiating the handshake with the SPDM server.
     * @param deviceID The DeviceID of the device to initiate the handshake
     * with.
     * @param connState The SPDM session connection state of the device.
     */
    void initiateSPDMHandshake(HandshakeCallback initiateHandshakeCallback,
                               DeviceID deviceID, bool connState);

    /**
     * @brief Send MCTP request to dstEId and receive status of send operation
     *
     * @param yield boost yiled_context object to yield on dbus calls
     * @param dstEId Destination MCTP Device ID
     * @param msgTag MCTP message tag value
     * @param tagOwner MCTP tag owner bit. Identifies whether the message tag
     * was originated by the endpoint that is the source of the message
     * @param request MCTP request byte array
     * @return std::pair<boost::system::error_code, int> Pair of boost
     * error_code and dbus send method call return value
     */
    std::pair<boost::system::error_code, int>
        sendYield(boost::asio::yield_context& yield, const DeviceID devID,
                  const uint8_t msgTag, const bool tagOwner,
                  const ByteArray& request);
    inline void setUseSocket(bool flag)
    {
        useSocket = flag;
    }
    std::optional<std::string> getDeviceLocation(const DeviceID eid);
    void getOwnEIDs(OwnEIDChangeCallback callback);
    void setExtendedReceiveCallback(ExtendedReceiveMessageCallback callback);

  private:
    /* contains inforamtion about all the endpoints in BMC and their
     * information.
     */
    std::unordered_map<DeviceID, EndpointInfo> allEndpoints;
    std::unordered_map<std::string, NetworkID> ifaceNetworkMap;
    bool useSocket = false;
    std::map<boost::asio::generic::datagram_protocol::endpoint,
             std::shared_ptr<boost::asio::generic::datagram_protocol::socket>>
        boundSockets;

    void setupEndpoints(std::optional<boost::asio::yield_context>);
    void setupDbusListener();
    std::unique_ptr<sdbusplus::bus::match::match> mctpChangesWatch{};
    std::unique_ptr<sdbusplus::bus::match::match> vdmMessagesWatch{};
    std::unique_ptr<sdbusplus::bus::match::match> spdmWatch{};
    void onMCTPEvent(sdbusplus::message::message& msg);
    void onNewInterface(sdbusplus::message::message& msg);
    void onInterfaceRemoved(sdbusplus::message::message& msg);
    void onMessageReceived(sdbusplus::message::message& msg);
    void onPropertiesChanged(sdbusplus::message::message& msg);

    void handleEndpointAddition(
        const std::string objectPath,
        const DictType<std::string,
                       DictType<std::string, MctpPropertiesVariantType>>&
            values);
    void handleEndpointRemoval(const std::string objectPath,
                               const std::vector<std::string>& dbusInterfaces);

    void handleIfaceAddition(
        const std::string objectPath,
        const DictType<std::string,
                       DictType<std::string, MctpPropertiesVariantType>>&
            values);
    void handleIfaceRemoval(const std::string objectPath,
                            const std::vector<std::string>& dbusInterfaces);

    void handleSPDMEndpointAddition(
        const std::string objectPath,
        const DictType<std::string,
                       DictType<std::string, MctpPropertiesVariantType>>&
            values);
    void handleSPDMEndpointRemoval(
        const std::string objectPath,
        const std::vector<std::string>& dbusInterfaces);

    BindingType estimateBindingType(const uint32_t networkId);
    bool eligibleForReconfigurationCallback(const EndpointInfo& epInfo);
    void handleIncomingMessage(
        std::shared_ptr<boost::asio::generic::datagram_protocol::socket>,
        const boost::system::error_code&);
    void bindToEndpoint(
        boost::asio::generic::datagram_protocol::endpoint bindEndpoint);
    bool isOwnEid(mctpw::DeviceID devID);
};
} // namespace mctpw
