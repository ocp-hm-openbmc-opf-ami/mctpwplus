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

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <sdbusplus/asio/connection.hpp>
#include <string>
#include <utility>

namespace mctpw
{
class MCTPImpl;
/// MCTP Endpoint Id
using eid_t = uint8_t;
using ByteArray = std::vector<uint8_t>;
using NetworkID = uint8_t;
using LocalEID = eid_t;

struct DeviceID
{
    DeviceID() = default;
    constexpr DeviceID(LocalEID eidVal, NetworkID nwid) :
        id((nwid << 8) | eidVal)
    {
    }
    uint32_t id;
    bool operator<(const DeviceID& rhs) const
    {
        return id < rhs.id;
    }
    constexpr bool operator==(const DeviceID& rhs) const
    {
        return id == rhs.id;
    }
    constexpr LocalEID mctpEID() const
    {
        return id & 0xFF;
    }
    constexpr NetworkID networkId() const
    {
        constexpr size_t eidBits = 8;
        return id >> eidBits;
    }
};
} // namespace mctpw

namespace std
{
template <>
struct hash<mctpw::DeviceID>
{
    size_t operator()(const mctpw::DeviceID& val) const
    {
        return std::hash<decltype(val.id)>()(val.id);
    }
};
} // namespace std

namespace mctpw
{
struct VersionFields
{
    uint8_t major;
    uint8_t minor;
    uint8_t update;
    uint8_t alpha;
} __attribute__((__packed__));

/**
 * @brief MCTP Binding Type
 *
 */
enum class BindingType : uint8_t
{
    mctpOverSmBus = 0x01,
    mctpOverPcieVdm = 0x02,
    mctpOverUsb = 0x03,
    mctpOverKcs = 0x04,
    mctpOverSerial = 0x05,
    mctpOverI3C = 0x06,
    vendorDefined = 0xFF,
};

/**
 * @brief MCTP Message Type
 *
 */
enum class MessageType : uint8_t
{
    /** @brief Platform Level Data Model over MCTP */
    pldm = 0x01,
    /** @brief NC-SI over MCTP */
    ncsi = 0x02,
    /** @brief Ethernet over MCTP */
    ethernet = 0x03,
    /** @brief NVM Express Management Messages over MCTP */
    nvmeMgmtMsg = 0x04,
    /** @brief Security Protocol and Data Model over MCTP */
    spdm = 0x05,
    /** @brief Secure Messaging Protocol and Data Model over MCTP */
    securedMsg = 0x06,
    /** @brief CXL FM API over MCTP */
    cxlFmApi = 0x07,
    /** @brief CXL CCI over MCTP */
    cxlCci = 0x08,
    /** @brief Vendor Defined PCI */
    vdpci = 0x7E,
    /** @brief Vendor Defined IANA */
    vdiana = 0x7F,
};

/**
 * @brief Configuration values to create MCTPWrapper
 *
 */
struct MCTPConfiguration
{
    /**
     * @brief Construct a new MCTPConfiguration object with default values
     *
     */
    MCTPConfiguration() = default;
    /**
     * @brief Construct a new MCTPConfiguration object
     *
     * @param msgType MCTP message type
     * @param binding MCTP binding type
     */
    MCTPConfiguration(MessageType msgType, BindingType binding);
    /**
     * @brief Construct a new MCTPConfiguration object
     *
     * @param msgType MCTP message type. Only VDPCI supported now with vendor
     * defined parameters
     * @param binding MCTP binding type
     * @param vid Vendor Id
     */
    MCTPConfiguration(MessageType msgType, BindingType binding, uint16_t vid);
    /**
     * @brief Construct a new MCTPConfiguration object
     *
     * @param msgType MCTP message type. Only VDPCI supported now with vendor
     * defined parameters
     * @param binding MCTP binding type
     * @param vid Vendor Id
     * @param vendorMsgType Vendor defined message type
     * @param vendorMsgTypeMask Vendor defines message type mask
     */
    MCTPConfiguration(MessageType msgType, BindingType binding, uint16_t vid,
                      uint16_t vendorMsgType, uint16_t vendorMsgTypeMask);
    /// MCTP message type
    MessageType type;
    /// MCTP binding type
    BindingType bindingType;

    struct VendorMessageType
    {
        VendorMessageType(uint16_t vendorMsgType, uint16_t vendorMsgTypeMask) :
            value(vendorMsgType), mask(vendorMsgTypeMask)
        {
        }

        constexpr uint16_t cmdSetType() const
        {
            return value & mask;
        }

        /// Vendor defined message type
        uint16_t value;
        /// Vendor defined message mask
        uint16_t mask;
    };

    /// Vendor Id
    std::optional<uint16_t> vendorId = std::nullopt;
    std::optional<VendorMessageType> vendorMessageType = std::nullopt;

    /**
     * @brief Set vendor id. Input values are expected to be in CPU byte order
     *
     * @param vid Vendor Id
     */
    inline void setVendorId(uint16_t vid)
    {
        this->vendorId = std::make_optional<uint16_t>(htobe16(vid));
    }

    /**
     * @brief Set vendor defined message type. Input values are expected to be
     * in CPU byte order
     *
     * @param msgType Vendor Message Type
     * @param mask Vednor Message Type Mask
     */
    inline void setVendorMessageType(uint16_t msgType, uint16_t mask)
    {
        this->vendorMessageType = std::make_optional<VendorMessageType>(
            htobe16(msgType), htobe16(mask));
    }
};

struct Event
{
    enum class EventType : uint8_t
    {
        deviceAdded,
        deviceRemoved
        // TODO. Adding this enum value breaks dependent recipes on -Wall
        // ownEIDChange
    };
    EventType type;
    eid_t eid;
    DeviceID deviceId;
};

struct OwnEIDChange
{
    // This structure is expected to change in future. Thus providing a void* to
    // have backward compatibility /avoid ABI breaks.
    struct EIDChangeData
    {
        eid_t eid;
        std::string service;
    };
    void* context;
};

using ReconfigurationCallback =
    std::function<void(void*, const Event&, boost::asio::yield_context& yield)>;
using ReceiveMessageCallback =
    std::function<void(void*, eid_t, bool, uint8_t, const ByteArray&, int)>;
using ExtendedReceiveMessageCallback =
    std::function<void(void*, DeviceID, bool, uint8_t, const ByteArray&, int)>;
using OwnEIDChangeCallback = std::function<void(OwnEIDChange&)>;

/**
 * @brief Wrapper class to access MCTP functionalities
 *
 */
class MCTPWrapper
{
  public:
    using StatusCallback =
        std::function<void(boost::system::error_code, void*)>;
    /* Endpoint map entry: LocalEID, pair(bus,service) */
    using EndpointMap =
        std::unordered_map<eid_t, std::pair<unsigned, std::string>>;
    /* Endpoint map entry: DeviceID, pair(bus,service) */
    using EndpointMapExtended =
        std::unordered_map<DeviceID, std::pair<unsigned, std::string>>;
    using ReceiveCallback =
        std::function<void(boost::system::error_code, ByteArray&)>;
    using SendCallback = std::function<void(boost::system::error_code, int)>;

    /**
     * @brief Construct a new MCTPWrapper object
     *
     * @param ioContext boost io_context object. Usable if invoker is an sdbus
     * unaware app.
     * @param configIn MCTP configuration to describe message type and vendor
     * specific data if required.
     * @param networkChangeCb Callback to be executed when a network change
     * occurs in the system. For example a new device is inserted or removed etc
     * @param rxCb Callback to be executed when new MCTP message is
     * received.
     */
    MCTPWrapper(boost::asio::io_context& ioContext,
                const MCTPConfiguration& configIn,
                const ReconfigurationCallback& networkChangeCb = nullptr,
                const ReceiveMessageCallback& rxCb = nullptr);
    /**
     * @brief Construct a new MCTPWrapper object
     *
     * @param conn shared_ptr to already existing boost asio::connection
     * object. Usable if invoker is sdbus aware and uses asio::connection for
     * some other purposes.
     * @param configIn MCTP configuration to describe message type and vendor
     * specific data if required.
     * @param networkChangeCb Callback to be executed when a network change
     * occurs in the system. For example a new device is inserted or removed etc
     * @param rxCb Callback to be executed when new MCTP message is
     * received.
     */
    MCTPWrapper(std::shared_ptr<sdbusplus::asio::connection> conn,
                const MCTPConfiguration& configIn,
                const ReconfigurationCallback& networkChangeCb = nullptr,
                const ReceiveMessageCallback& rxCb = nullptr);

    /**
     * @brief Destroy the MCTPWrapper object
     *
     */
    ~MCTPWrapper() noexcept;
    /**
     * @brief This method or its yield variant must be called before accessing
     * any send receive functions. It scan and detect all mctp endpoints exposed
     * on dbus.
     *
     * @param callback Callback to be invoked after mctp endpoint detection with
     * status of the operation
     */
    void detectMctpEndpointsAsync(StatusCallback&& callback);
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
     * @brief Get a reference to internaly maintained EndpointMap without
     * network id
     *
     * @return const EndpointMap&
     */
    const EndpointMap& getEndpointMap();
    /**
     * @brief Get a reference to internaly maintained EndpointMap
     *
     * @return EndpointMapExtended
     */
    const EndpointMapExtended& getEndpointMapExtended();

    /**
     * @brief Trigger MCTP device discovery
     * @param dstEId Destination MCTP EID
     *
     */
    void triggerMCTPDeviceDiscovery(const eid_t dstEId);
    /**
     * @brief Trigger MCTP device discovery
     * @param devID Destination MCTP Device ID
     *
     */
    void triggerMCTPDeviceDiscovery(const DeviceID devID);

    /**
     * @brief Reserve bandwidth for EID
     *
     * @param yield Boost yield_context to use on dbus call
     * @param dstEId Destination MCTP EID
     * @param timeout reserve bandwidth timeout
     * @return dbus send method call return value
     */
    int reserveBandwidth(boost::asio::yield_context yield, const eid_t dstEId,
                         const uint16_t timeout);
    /**
     * @brief Reserve bandwidth for DeviceID
     *
     * @param yield Boost yield_context to use on dbus call
     * @param devID Destination MCTP Device ID
     * @param timeout reserve bandwidth timeout
     * @return dbus send method call return value
     */
    int reserveBandwidth(boost::asio::yield_context yield,
                         const DeviceID devID, const uint16_t timeout);

    /**
     * @brief Release bandwidth for EID
     *
     * @param yield Boost yield_context to use on dbus call
     * @param dstEId Destination MCTP EID
     * @return dbus send method call return value
     */
    int releaseBandwidth(boost::asio::yield_context yield, const eid_t dstEId);
    /**
     * @brief Release bandwidth for EID
     *
     * @param yield Boost yield_context to use on dbus call
     * @param devID Destination MCTP Device ID
     * @return dbus send method call return value
     */
    int releaseBandwidth(boost::asio::yield_context yield,
                         const DeviceID devID);

    /**
     * @brief Send request to dstEId and receive response asynchronously in
     * receiveCb
     *
     * @param receiveCb Callback to be executed when response is ready
     * @param dstEId Destination MCTP EID
     * @param request MCTP request byte array
     * @param timeout MCTP receive timeout
     */
    void sendReceiveAsync(ReceiveCallback receiveCb, eid_t dstEId,
                          const ByteArray& request,
                          std::chrono::milliseconds timeout);
    /**
     * @brief Send request to devID and receive response asynchronously in
     * receiveCb
     *
     * @param receiveCb Callback to be executed when response is ready
     * @param devID Destination MCTP Device ID
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
     * @param dstEId Destination MCTP EID
     * @param request MCTP request byte array
     * @param timeout MCTP receive timeout
     * @return std::pair<boost::system::error_code, ByteArray> Pair of boost
     * error code and response byte array
     */
    std::pair<boost::system::error_code, ByteArray>
        sendReceiveYield(boost::asio::yield_context yield, eid_t dstEId,
                         const ByteArray& request,
                         std::chrono::milliseconds timeout);
    /**
     * @brief Send request to devID and receive response using yield_context
     *
     * @param yield Boost yield_context to use on dbus call
     * @param devID Destination MCTP Device ID
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
     * @brief Send request to dstEId and receive response using
     * a blocked call
     * @param dstEId Destination MCTP EID
     * @param request MCTP request byte array
     * @param timeout MCTP receive timeout
     * @return std::pair<boost::system::error_code, ByteArray> Pair of boost
     * error code and response byte array
     */
    std::pair<boost::system::error_code, ByteArray>
        sendReceiveBlocked(eid_t dstEId, const ByteArray& request,
                           std::chrono::milliseconds timeout);
    /**
     * @brief Send request to devID and receive response using
     * a blocked call
     * @param devID Destination MCTP Device ID
     * @param request MCTP request byte array
     * @param timeout MCTP receive timeout
     * @return std::pair<boost::system::error_code, ByteArray> Pair of boost
     * error code and response byte array
     */
    std::pair<boost::system::error_code, ByteArray>
        sendReceiveBlocked(DeviceID devID, const ByteArray& request,
                           std::chrono::milliseconds timeout);
    /**
     * @brief Send MCTP request to dstEId and receive status of send operation
     * in callback
     *
     * @param callback Callback that will be invoked with status of send
     * operation
     * @param dstEId Destination MCTP EID
     * @param msgTag MCTP message tag value
     * @param tagOwner MCTP tag owner bit. Identifies whether the message tag
     * was originated by the endpoint that is the source of the message
     * @param request MCTP request byte array
     */
    void sendAsync(const SendCallback& callback, const eid_t dstEId,
                   const uint8_t msgTag, const bool tagOwner,
                   const ByteArray& request);
    /**
     * @brief Send MCTP request to devID and receive status of send operation
     * in callback
     *
     * @param callback Callback that will be invoked with status of send
     * operation
     * @param devID Destination MCTP Device ID
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
     * @param dstEId Destination MCTP EID
     * @param msgTag MCTP message tag value
     * @param tagOwner MCTP tag owner bit. Identifies whether the message tag
     * was originated by the endpoint that is the source of the message
     * @param request MCTP request byte array
     * @return std::pair<boost::system::error_code, int> Pair of boost
     * error_code and dbus send method call return value
     */
    std::pair<boost::system::error_code, int>
        sendYield(boost::asio::yield_context& yield, const eid_t dstEId,
                  const uint8_t msgTag, const bool tagOwner,
                  const ByteArray& request);    /**
     * @brief Send MCTP request to devID and receive status of send operation
     *
     * @param yield boost yiled_context object to yield on dbus calls
     * @param devID Destination MCTP Device ID
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

    /**
     * @brief Register a responder application with MCTP layer
     * @param version The version supported by the responder. Use if only one
     * version is supported
     * @return boost error code
     */
    boost::system::error_code registerResponder(VersionFields version);
    /**
     * @brief Register a responder application with MCTP layer
     * @param versions List of versions supported by the responder. Use if
     * multiple versions are supported
     * @return boost error code
     */
    boost::system::error_code
        registerResponder(const std::vector<VersionFields>& versions);

    /**
     * @brief Get human-readable device location string by EID
     *
     * When device location string is not available or it is an empty string,
     * will return std::nullopt.
     *
     * @param eid MCTP Endpoint ID of the device to query
     * @return std::optional<std::string> Optional device location string
     */
    std::optional<std::string> getDeviceLocation(const eid_t eid);
    /**
     * @brief Get human-readable device location string by EID
     *
     * When device location string is not available or it is an empty string,
     * will return std::nullopt.
     *
     * @param eid MCTP Endpoint ID of the device to query
     * @return std::optional<std::string> Optional device location string
     */
    std::optional<std::string> getDeviceLocation(const DeviceID eid);

    /**
     * @brief Get own eid on each available mctp services
     *
     * Multiple mctp services will be running in the system. This method
     * will invoke the callback for each mctp service with its own eid.
     * Also whenever the eid changes on a service the same callback will
     * be executed with the new eid and related info.
     *
     * @param callback For each own eid available among mctp services this
     * callback will be executed
     * @return void
     */
    void getOwnEIDs(OwnEIDChangeCallback callback);

    /**
     * @bried This callback will be executed when an mctp message is received
     * with tagowner not set and there is no pending request in mctpd queue
     * @param callback Callback function
     */
    void setExtendedReceiveCallback(ExtendedReceiveMessageCallback callback);

    /// MCTP Configuration to store message type and vendor defined properties
    MCTPConfiguration config{};

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

    static const inline std::unordered_map<BindingType, const std::string>
        bindingToInterface = {
            {BindingType::mctpOverSmBus,
             "xyz.openbmc_project.MCTP.Binding.SMBus"},
            {BindingType::mctpOverPcieVdm,
             "xyz.openbmc_project.MCTP.Binding.PCIe"},
            {BindingType::mctpOverUsb, ""},
            {BindingType::mctpOverKcs, ""},
            {BindingType::mctpOverSerial, ""},
            {BindingType::mctpOverI3C, "xyz.openbmc_project.MCTP.Binding.I3C"},
            {BindingType::vendorDefined, ""}};

  private:
    std::unique_ptr<MCTPImpl> pimpl;
};

} // namespace mctpw
