/*
// Copyright (c) 2021 Intel Corporation
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

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
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

namespace internal
{
struct NewServiceCallback;
struct DeleteServiceCallback;
} // namespace internal

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
     * @param ioContext boost io_context object
     * @param configIn MCTP configuration to describe message type and vendor
     * specific data if required.
     * @param networkChangeCb Callback to be executed when a network change
     * occurs in the system. For example a new device is inserted or removed etc
     * @param rxCb Callback to be executed when new MCTP message is
     */
    MCTPImpl(boost::asio::io_context& ioContext,
             const MCTPConfiguration& configIn,
             const ReconfigurationCallback& networkChangeCb,
             const ReceiveMessageCallback& rxCb);
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

    using StatusCallback =
        std::function<void(boost::system::error_code, void*)>;

    /* Endpoint map entry: DeviceID,pair(bus,service) */
    using EndpointMapExtended = MCTPWrapper::EndpointMapExtended;

    using ReceiveCallback =
        std::function<void(boost::system::error_code, ByteArray&)>;
    using SendCallback = std::function<void(boost::system::error_code, int)>;

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
                                 {MessageType::vdiana, "VDIANA"}};
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
     * @brief This method or its async variant must be called before accessing
     * any send receive functions. It scan and detect all mctp endpoints exposed
     * on dbus.
     *
     * @param yield boost yield_context object to yield on dbus calls
     * @return boost::system::error_code
     */
    boost::system::error_code
        detectMctpEndpoints(boost::asio::yield_context yield);
    /**
     * @brief Get a reference to internaly maintained EndpointMap
     *
     * @return const EndpointMapExtended&
     */
    inline const EndpointMapExtended& getEndpointMap() const
    {
        return this->endpointMap;
    }

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
    int releaseBandwidth(boost::asio::yield_context yield, const DeviceID devID);

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
    void addToEidMap(boost::asio::yield_context yield,
                     const std::string& serviceName/*, uint16_t vid,
                     uint16_t vmsgType*/);
    size_t eraseDevice(DeviceID eid);
    std::optional<std::string> getDeviceLocation(const DeviceID eid);
    void getOwnEIDs(OwnEIDChangeCallback callback);
    void setExtendedReceiveCallback(ExtendedReceiveMessageCallback callback);

  private:
    EndpointMapExtended endpointMap;
    std::unordered_set<std::string> matchedBuses;
    std::vector<VersionFields> responderVersions;
    // Get list of pair<bus, service_name_string> which expose mctp object
    std::optional<std::vector<std::pair<unsigned, std::string>>>
        findBusByBindingType(boost::asio::yield_context yield);
    /* Return format: map<Eid, pair<bus, service_name_string>> */
    EndpointMapExtended buildMatchingEndpointMap(
        boost::asio::yield_context yield,
        std::vector<std::pair<unsigned, std::string>>& buses);
    // Get bus id from servicename. Example: Returns 2 if device path is
    // /dev/i2c-2
    int getBusId(const std::string& serviceName);

    void listenForMCTPChanges();
    std::unique_ptr<sdbusplus::bus::match::match> mctpChangesWatch{};
    void onMCTPEvent(sdbusplus::message::message& msg);
    void onNewInterface(sdbusplus::message::message& msg);
    void onInterfaceRemoved(sdbusplus::message::message& msg);
    void onMessageReceived(sdbusplus::message::message& msg);
    void onPropertiesChanged(sdbusplus::message::message& msg);
    void onNewService(const std::string& serviceName);
    void onNewEID(const std::string& serviceName, DeviceID eid);
    void onOwnEIDChange(std::string serviceName, eid_t eid);
    void onEIDRemoved(DeviceID eid);
    void addUniqueNameToMatchedServices(const std::string& serviceName, boost::asio::yield_context yield);
    
    void registerListeners(const std::string& serviceName);
    void unRegisterListeners(const std::string& serviceName);

    void triggerGetOwnEID(const std::string& serviceName);
    boost::system::error_code registerResponder(const std::string& serviceName);
    friend struct internal::NewServiceCallback;
    friend struct internal::DeleteServiceCallback;

    std::unordered_map<std::string, uint8_t> networkIDCache;
    uint8_t getNetworkID(const std::string& serviceName);
    DeviceID
        getDeviceIDFromPath(const sdbusplus::message::object_path& objectPath,
                            const std::string& serviceName);
};
} // namespace mctpw
