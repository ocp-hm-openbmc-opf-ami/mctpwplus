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

#include "mctp_wrapper.hpp"

#include "mctp_impl.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/container/flat_map.hpp>
#include <memory>
#include <phosphor-logging/log.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus/match.hpp>
#include <unordered_set>

using namespace mctpw;

template <typename T1, typename T2>
using DictType = boost::container::flat_map<T1, T2>;
using MctpPropertiesVariantType =
    std::variant<uint16_t, int16_t, int32_t, uint32_t, bool, std::string,
                 uint8_t, std::vector<uint8_t>>;

MCTPConfiguration::MCTPConfiguration(MessageType msgType, BindingType binding) :
    type(msgType), bindingType(binding)
{
}

MCTPConfiguration::MCTPConfiguration(MessageType msgType, BindingType binding,
                                     uint16_t vid) :
    type(msgType),
    bindingType(binding)
{
    if (MessageType::vdpci != msgType)
    {
        throw std::invalid_argument("MsgType expected VDPCI");
    }
    setVendorId(vid);
}

MCTPConfiguration::MCTPConfiguration(MessageType msgType, BindingType binding,
                                     uint16_t vid, uint16_t vendorMsgType,
                                     uint16_t vendorMsgTypeMask) :
    MCTPConfiguration(msgType, binding, vid)
{
    setVendorMessageType(vendorMsgType, vendorMsgTypeMask);
}

MCTPWrapper::MCTPWrapper(boost::asio::io_context& ioContext,
                         const MCTPConfiguration& configIn,
                         const ReconfigurationCallback& networkChangeCb,
                         const ReceiveMessageCallback& rxCb) :
    config(configIn),
    pimpl(
        std::make_unique<MCTPImpl>(ioContext, configIn, networkChangeCb, rxCb))
{
}

MCTPWrapper::MCTPWrapper(std::shared_ptr<sdbusplus::asio::connection> conn,
                         const MCTPConfiguration& configIn,
                         const ReconfigurationCallback& networkChangeCb,
                         const ReceiveMessageCallback& rxCb) :
    config(configIn),
    pimpl(std::make_unique<MCTPImpl>(conn, configIn, networkChangeCb, rxCb))
{
}

MCTPWrapper::~MCTPWrapper() noexcept = default;

void MCTPWrapper::detectMctpEndpointsAsync(StatusCallback&& registerCB)
{
    pimpl->detectMctpEndpointsAsync(std::forward<StatusCallback>(registerCB));
}

boost::system::error_code
    MCTPWrapper::detectMctpEndpoints(boost::asio::yield_context yield)
{
    boost::system::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::success);
    ec = pimpl->detectMctpEndpoints(yield);

    return ec;
}

void MCTPWrapper::sendReceiveAsync(ReceiveCallback callback, eid_t dstEId,
                                   const ByteArray& request,
                                   std::chrono::milliseconds timeout)
{
    sendReceiveAsync(callback, DeviceID(dstEId, 0), request, timeout);
}

void MCTPWrapper::sendReceiveAsync(ReceiveCallback callback, DeviceID extendedEID,
                                   const ByteArray& request,
                                   std::chrono::milliseconds timeout)
{
    pimpl->sendReceiveAsync(callback, extendedEID, request, timeout);
}

std::pair<boost::system::error_code, ByteArray>
    MCTPWrapper::sendReceiveYield(boost::asio::yield_context yield,
                                  eid_t dstEId, const ByteArray& request,
                                  std::chrono::milliseconds timeout)
{
    return sendReceiveYield(yield, DeviceID(dstEId, 0), request, timeout);
}

std::pair<boost::system::error_code, ByteArray>
    MCTPWrapper::sendReceiveYield(boost::asio::yield_context yield,
                                  DeviceID extendedEID, const ByteArray& request,
                                  std::chrono::milliseconds timeout)
{
    return pimpl->sendReceiveYield(yield, extendedEID, request, timeout);
}

std::pair<boost::system::error_code, ByteArray>
    MCTPWrapper::sendReceiveBlocked(eid_t dstEId, const ByteArray& request,
                                    std::chrono::milliseconds timeout)
{
    return sendReceiveBlocked(DeviceID(dstEId, 0), request, timeout);
}

std::pair<boost::system::error_code, ByteArray>
    MCTPWrapper::sendReceiveBlocked(DeviceID extendedEID, const ByteArray& request,
                                    std::chrono::milliseconds timeout)
{
    return pimpl->sendReceiveBlocked(extendedEID, request, timeout);
}

boost::system::error_code MCTPWrapper::registerResponder(VersionFields version)
{
    return pimpl->registerResponder(version);
}

boost::system::error_code
    MCTPWrapper::registerResponder(const std::vector<VersionFields>& versions)
{
    return pimpl->registerResponder(versions);
}

void MCTPWrapper::sendAsync(const SendCallback& callback, const eid_t dstEId,
                            const uint8_t msgTag, const bool tagOwner,
                            const ByteArray& request)
{
    sendAsync(callback, DeviceID(dstEId, 0), msgTag, tagOwner, request);
}

void MCTPWrapper::sendAsync(const SendCallback& callback, const DeviceID extendedEID,
                            const uint8_t msgTag, const bool tagOwner,
                            const ByteArray& request)
{
    pimpl->sendAsync(callback, extendedEID, msgTag, tagOwner, request);
}

std::pair<boost::system::error_code, int>
    MCTPWrapper::sendYield(boost::asio::yield_context& yield,
                           const eid_t dstEId, const uint8_t msgTag,
                           const bool tagOwner, const ByteArray& request)
{
    return sendYield(yield, DeviceID(dstEId, 0), msgTag, tagOwner, request);
}

std::pair<boost::system::error_code, int>
    MCTPWrapper::sendYield(boost::asio::yield_context& yield,
                           const DeviceID extendedEID, const uint8_t msgTag,
                           const bool tagOwner, const ByteArray& request)
{
    return pimpl->sendYield(yield, extendedEID, msgTag, tagOwner, request);
}

const MCTPWrapper::EndpointMap& MCTPWrapper::getEndpointMap()
{
    auto& extendedMap = pimpl->getEndpointMap();
    static EndpointMap localEidMap;
    for (auto [deviceId, service] : extendedMap)
    {
        localEidMap.emplace(deviceId.mctpEID(), service);
    }
    return localEidMap;
}

const MCTPWrapper::EndpointMapExtended& MCTPWrapper::getEndpointMapExtended()
{
    return pimpl->getEndpointMap();
}

void MCTPWrapper::triggerMCTPDeviceDiscovery(const eid_t dstEId)
{
    triggerMCTPDeviceDiscovery(DeviceID(dstEId, 0));
}

void MCTPWrapper::triggerMCTPDeviceDiscovery(const DeviceID extendedEID)
{
    pimpl->triggerMCTPDeviceDiscovery(extendedEID);
}

int MCTPWrapper::reserveBandwidth(boost::asio::yield_context yield,
                                  const eid_t dstEId, const uint16_t timeout)
{
    return reserveBandwidth(yield, DeviceID(dstEId, 0), timeout);
}

int MCTPWrapper::reserveBandwidth(boost::asio::yield_context yield,
                                  const DeviceID extendedEID, const uint16_t timeout)
{
    return pimpl->reserveBandwidth(yield, extendedEID, timeout);
}

int MCTPWrapper::releaseBandwidth(boost::asio::yield_context yield,
                                  const eid_t dstEId)
{
    return releaseBandwidth(yield, DeviceID(dstEId, 0));
}

int MCTPWrapper::releaseBandwidth(boost::asio::yield_context yield,
                                  const DeviceID extendedEID)
{
    return pimpl->releaseBandwidth(yield, extendedEID);
}

std::optional<std::string> MCTPWrapper::getDeviceLocation(const eid_t eid)
{
    return pimpl->getDeviceLocation(DeviceID(eid, 0));
}
std::optional<std::string> MCTPWrapper::getDeviceLocation(const DeviceID extendedEID)
{
    return pimpl->getDeviceLocation(extendedEID);
}

void MCTPWrapper::getOwnEIDs(OwnEIDChangeCallback callback)
{
    pimpl->getOwnEIDs(callback);
}

void MCTPWrapper::setExtendedReceiveCallback(
    ExtendedReceiveMessageCallback callback)
{
    pimpl->setExtendedReceiveCallback(callback);
}