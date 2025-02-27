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

#include "mctp_impl.hpp"

#include "utils.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/container/flat_map.hpp>
#include <phosphor-logging/log.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus/match.hpp>
#include <unordered_set>

template <typename T1, typename T2>
using DictType = boost::container::flat_map<T1, T2>;
using MctpPropertiesVariantType =
    std::variant<uint16_t, int16_t, int32_t, uint32_t, bool, std::string,
                 uint8_t, std::vector<uint8_t>, std::vector<uint16_t>>;

namespace mctpw
{
void MCTPImpl::detectMctpEndpointsAsync(StatusCallback&& registerCB)
{
    boost::asio::spawn(connection->get_io_context(),
                       [registerCB = std::move(registerCB),
                        this](boost::asio::yield_context yield) {
                           auto ec = detectMctpEndpoints(yield);
                           if (registerCB)
                           {
                               registerCB(ec, this);
                           }
                       },
                       {});
}

void MCTPImpl::triggerMCTPDeviceDiscovery(const DeviceID devID)
{
    auto it = this->endpointMap.find(devID);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "triggerMCTPDeviceDiscovery: EID not found in end point map",
            phosphor::logging::entry("EID=%d", devID.id));
        return;
    }

    connection->async_method_call(
        [](boost::system::error_code ec) {
            if (ec)
            {
                phosphor::logging::log<phosphor::logging::level::ERR>(
                    ("MCTP device discovery error: " + ec.message()).c_str());
            }
        },
        it->second, "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "TriggerDeviceDiscovery");
}

int MCTPImpl::reserveBandwidth(boost::asio::yield_context yield,
                               const DeviceID devID, const uint16_t timeout)
{
    auto it = this->endpointMap.find(devID);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("reserveBandwidth: EID not found in end point map" +
             std::to_string(devID.id))
                .c_str());
        return -1;
    }
    boost::system::error_code ec;
    int status = connection->yield_method_call<int>(
        yield, ec, it->second, "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "ReserveBandwidth", devID.mctpEID(),
        timeout);

    if (ec)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("ReserveBandwidth: failed for EID: " + std::to_string(devID.id) +
             " " + ec.message())
                .c_str());
        return -1;
    }
    else if (status < 0)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("ReserveBandwidth: failed for EID: " + std::to_string(devID.id) +
             " rc: " + std::to_string(status))
                .c_str());
    }
    return status;
}

int MCTPImpl::releaseBandwidth(boost::asio::yield_context yield,
                               const DeviceID devID)
{
    auto it = this->endpointMap.find(devID);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("ReleaseBandwidth: EID not found in end point map" +
             std::to_string(devID.id))
                .c_str());
        return -1;
    }
    boost::system::error_code ec;
    int status = connection->yield_method_call<int>(
        yield, ec, it->second, "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "ReleaseBandwidth", devID.mctpEID());
    if (ec)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("ReleaseBandwidth: failed for EID: " + std::to_string(devID.id) +
             " " + ec.message())
                .c_str());
        return -1;
    }
    else if (status < 0)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("ReleaseBandwidth: failed for EID: " + std::to_string(devID.id) +
             " rc: " + std::to_string(status))
                .c_str());
    }

    return status;
}

boost::system::error_code MCTPImpl::detectMctpEndpoints(
    std::optional<boost::asio::yield_context> yield = std::nullopt)
{
    phosphor::logging::log<phosphor::logging::level::DEBUG>(
        "Detecting mctp endpoints");
    listenForMCTPChanges();

    boost::system::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::success);
    auto bus_vector = findBusByBindingType(yield);
    if (bus_vector)
    {
        buildMatchingEndpointMap(yield, bus_vector.value());
        if (useSocket)
        {
            for (auto& it : endpointMap)
            {
                auto service = it.second;
                std::string socketPath = readPropertyValue<std::string>(
                    *connection, service, "/xyz/openbmc_project/mctp",
                    "xyz.openbmc_project.MCTP.Base", "SocketPath");
                auto sktInt = std::make_shared<SocketInterface>(
                    socketPath, this->connection->get_io_context());
                sktInt->setMessageReceivedCallback(
                    [this](eid_t eid, bool tagOwner, uint8_t msgTag,
                           const ByteArray& payload) {
                        if (receiveCallback)
                        {
                            receiveCallback(this, eid, tagOwner, msgTag,
                                            payload, 0);
                        }
                    });
                socketIntf.insert(std::make_pair(service, std::move(sktInt)));
            }
        }
    }

    if (responderVersions.size() > 0)
    {
        phosphor::logging::log<phosphor::logging::level::INFO>(
            "Register responder was called before discovery");
        registerResponder(responderVersions);
    }

    if (this->eidChangeCallback)
    {
        // getOwnEID was called before. Retrigger the events
        this->getOwnEIDs(this->eidChangeCallback);
    }

    phosphor::logging::log<phosphor::logging::level::DEBUG>(
        ("Detecting mctp endpoints completed. Found " +
         std::to_string(endpointMap.size()))
            .c_str());
    return ec;
}

void MCTPImpl::addUniqueNameToMatchedServices(
    const std::string& serviceName,
    std::optional<boost::asio::yield_context> yield)
{
    auto uniqueName = mctpw::methodCall<std::string>(
        *connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetNameOwner", yield, serviceName.c_str());

    if (!uniqueName)
    {
        std::string errMsg = std::string("GetUniqueName unsuccesful for ") +
                             serviceName + ". " + uniqueName.error().message();
        phosphor::logging::log<phosphor::logging::level::WARNING>(
            errMsg.c_str());
        uniqueName = serviceName;
    }

    this->matchedBuses.emplace(uniqueName.value());
}

std::optional<std::vector<std::string>> MCTPImpl::findBusByBindingType(
    std::optional<boost::asio::yield_context> yield)
{
    boost::system::error_code ec;
    std::vector<std::string> buses;
    DictType<std::string, std::vector<std::string>> services;
    std::vector<std::string> interfaces = {};
    try
    {
        auto intf =
            mctpw::MCTPWrapper::bindingToInterface.at(config.bindingType);
        if (!intf.empty())
        {
            interfaces.push_back(intf);
        }
        // find the services, with their interfaces, that implement a
        // certain object path

        auto getObjects = mctpw::methodCall<decltype(services)>(
            *connection, "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject", yield,
            "/xyz/openbmc_project/mctp", interfaces);
        if (getObjects)
        {
            services = getObjects.value();
        }
        else
        {
            throw std::runtime_error(
                (std::string("Error getting mctp services. ") +
                 getObjects.error().message())
                    .c_str());
        }

        for (const auto& [service, intfs] : services)
        {
            try
            {
                buses.emplace_back(service);
                addUniqueNameToMatchedServices(service, yield);
            }
            catch (const std::exception& e)
            {
                phosphor::logging::log<phosphor::logging::level::WARNING>(
                    e.what());
            }
        }
        // buses will contain list of {busid servicename}. Sample busid may
        // be from i2cdev-2
        return buses;
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            (std::string("findBusByBindingType: ") + e.what()).c_str());
        return std::nullopt;
    }
}

/* Return format:
 * map<Eid, pair<bus, service_name_string>>
 */
void MCTPImpl::buildMatchingEndpointMap(
    std::optional<boost::asio::yield_context> yield,
    std::vector<std::string> services)
{
    for (auto& service : services)
    {
        boost::system::error_code ec;
        DictType<sdbusplus::message::object_path,
                 DictType<std::string,
                          DictType<std::string, MctpPropertiesVariantType>>>
            values;
        // get all objects, interfaces and properties in a single method
        // call DICT<OBJPATH,DICT<STRING,DICT<STRING,VARIANT>>>
        // objpath_interfaces_and_properties

        auto getManagedObjects = mctpw::methodCall<decltype(values)>(
            *connection, service, "/xyz/openbmc_project/mctp",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", yield);

        if (getManagedObjects)
        {
            values = getManagedObjects.value();
        }
        else
        {
            phosphor::logging::log<phosphor::logging::level::WARNING>(
                (std::string("Error getting managed objects on ") + service +
                 ". Bus ")
                    .c_str());
            continue;
        }

        NetworkID nwid = getNetworkID(service);
        for (const auto& [objectPath, interfaces] : values)
        {
            DictType<std::string,
                     DictType<std::string, MctpPropertiesVariantType>>
                interface;

            if (interfaces.find("xyz.openbmc_project.MCTP.Endpoint") ==
                interfaces.end())
            {
                continue;
            }
            try
            {
                if (config.type != mctpw::MessageType::any)
                {
                    /*SupportedMessageTypes interface is mandatory*/
                    auto& msgIf = interfaces.at(
                        "xyz.openbmc_project.MCTP.SupportedMessageTypes");
                    MctpPropertiesVariantType pv;
                    pv = msgIf.at(msgTypeToPropertyName.at(config.type));

                    if (std::get<bool>(pv) == false)
                    {
                        continue;
                    }
                }
                if (mctpw::MessageType::vdpci == config.type)
                {
                    if (config.vendorId)
                    {
                        static const char* vdMsgTypeInterface =
                            "xyz.openbmc_project.MCTP.PCIVendorDefined";
                        auto vendorIdStr = readPropertyValue<std::string>(
                            *connection, service, objectPath.str,
                            vdMsgTypeInterface, "VendorID");
                        uint16_t vendorId = static_cast<uint16_t>(
                            std::stoi(vendorIdStr, nullptr, 16));
                        if (vendorId != be16toh(*config.vendorId))
                        {
                            phosphor::logging::log<
                                phosphor::logging::level::INFO>(
                                ("VendorID not matching for " + objectPath.str)
                                    .c_str());
                            continue;
                        }

                        if (config.vendorMessageType)
                        {
                            auto msgTypes =
                                readPropertyValue<std::vector<uint16_t>>(
                                    *connection, service, objectPath.str,
                                    vdMsgTypeInterface, "MessageTypeProperty");
                            auto itMsgType = std::find(
                                msgTypes.begin(), msgTypes.end(),
                                be16toh(config.vendorMessageType->value));
                            if (msgTypes.end() == itMsgType)
                            {
                                phosphor::logging::log<
                                    phosphor::logging::level::INFO>(
                                    ("Vendor Message Type not matching for " +
                                     objectPath.str)
                                        .c_str());
                                continue;
                            }
                        }
                    }
                    else
                    {
                        if (config.vendorMessageType)
                        {
                            phosphor::logging::log<
                                phosphor::logging::level::ERR>(
                                "Vendor Message Type matching is not allowed "
                                "when Vendor ID is not set");
                            continue;
                        }
                    }
                }
                /* format of of endpoint path: path/Eid */
                std::vector<std::string> splitted;
                boost::split(splitted, objectPath.str, boost::is_any_of("/"));
                if (splitted.size())
                {
                    // TODO: Check nwid and value in object path is same
                    /* take the last element and convert it to eid */
                    uint8_t eid = static_cast<eid_t>(
                        std::stoi(splitted[splitted.size() - 1]));
                    this->endpointMap[DeviceID(eid, nwid)] = service;
                }
            }
            catch (std::exception& e)
            {
                phosphor::logging::log<phosphor::logging::level::ERR>(e.what());
            }
        }
    }
}

void MCTPImpl::sendReceiveAsync(ReceiveCallback callback, DeviceID devID,
                                const ByteArray& request,
                                std::chrono::milliseconds timeout)
{
    ByteArray response;
    auto it = this->endpointMap.find(devID);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "SendReceiveAsync: Eid not found in end point map",
            phosphor::logging::entry("EID=%d", devID.id));
        boost::system::error_code ec =
            boost::system::errc::make_error_code(boost::system::errc::io_error);
        if (callback)
        {
            callback(ec, response);
        }
        return;
    }
    if (useSocket)
    {
        auto socket = this->socketIntf.find(it->second);
        if (socket != this->socketIntf.end())
        {
            socket->second->sendReceiveAsync(callback, devID.mctpEID(), request,
                                             timeout);
        }
    }
    else
    {
        connection->async_method_call(
            callback, it->second, "/xyz/openbmc_project/mctp",
            "xyz.openbmc_project.MCTP.Base", "SendReceiveMctpMessagePayload",
            devID.mctpEID(), request, static_cast<uint16_t>(timeout.count()));
    }
}

std::pair<boost::system::error_code, ByteArray>
    MCTPImpl::sendReceiveYield(boost::asio::yield_context yield, DeviceID devID,
                               const ByteArray& request,
                               std::chrono::milliseconds timeout)
{
    auto receiveResult = std::make_pair(
        boost::system::errc::make_error_code(boost::system::errc::success),
        ByteArray());
    auto it = this->endpointMap.find(devID);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "SendReceiveYield: Eid not found in end point map",
            phosphor::logging::entry("EID=%d", devID.id));
        receiveResult.first =
            boost::system::errc::make_error_code(boost::system::errc::io_error);
        return receiveResult;
    }
    if (useSocket)
    {
        auto socket = socketIntf.find(it->second);
        receiveResult = socket->second->sendReceiveYield(yield, devID.mctpEID(),
                                                         request, timeout);
    }
    else
    {
        receiveResult.second = connection->yield_method_call<ByteArray>(
            yield, receiveResult.first, it->second, "/xyz/openbmc_project/mctp",
            "xyz.openbmc_project.MCTP.Base", "SendReceiveMctpMessagePayload",
            devID.mctpEID(), request, static_cast<uint16_t>(timeout.count()));
    }
    return receiveResult;
}

boost::system::error_code
    MCTPImpl::registerResponder(const VersionFields& version)
{
    std::vector<VersionFields> versions = {version};
    return registerResponder(versions);
}

boost::system::error_code
    MCTPImpl::registerResponder(const std::vector<VersionFields>& specVersion)
{
    if (specVersion.empty())
    {
        return boost::system::errc::make_error_code(
            boost::system::errc::io_error);
    }
    responderVersions = specVersion;

    auto status =
        boost::system::errc::make_error_code(boost::system::errc::success);

    for (auto mctpdServiceName : matchedBuses)
    {
        status = registerResponder(mctpdServiceName);
        if (status != boost::system::errc::success)
        {
            phosphor::logging::log<phosphor::logging::level::DEBUG>(
                ("Error setting responder version in " + mctpdServiceName)
                    .c_str());
            continue;
        }
    }

    return status;
}

boost::system::error_code
    MCTPImpl::registerResponder(const std::string& serviceName)
{
    if (responderVersions.empty())
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "Responder version not set");
        return boost::system::errc::make_error_code(
            boost::system::errc::io_error);
    }
    auto status =
        boost::system::errc::make_error_code(boost::system::errc::success);

    phosphor::logging::log<phosphor::logging::level::DEBUG>(
        ("Registering responder version to service " + serviceName).c_str());

    bool rc = true;
    std::string registerMethod("RegisterResponder");

    if (config.type == mctpw::MessageType::vdpci)
    {
        registerMethod.assign("RegisterVdpciResponder");
    }

    std::vector<uint8_t> version(
        sizeof(VersionFields) * responderVersions.size(), 0);
    std::copy_n(reinterpret_cast<uint8_t*>(responderVersions.data()),
                sizeof(VersionFields) * responderVersions.size(),
                version.begin());

    auto msg = connection->new_method_call(
        serviceName.c_str(), "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", registerMethod.c_str());

    if (config.type == mctpw::MessageType::vdpci)
    {
        uint16_t cmdSetType = config.vendorMessageType->cmdSetType();
        msg.append(*config.vendorId);
        msg.append(cmdSetType);
        msg.append(version);
    }
    else
    {
        msg.append(static_cast<uint8_t>(config.type));
        msg.append(version);
    }

    try
    {
        auto reply = connection->call(msg);
        reply.read(rc);
        if (!rc)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Error in registering the responder");

            return boost::system::errc::make_error_code(
                boost::system::errc::io_error);
        }
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Unable to register responder. Error");
        return boost::system::errc::make_error_code(
            boost::system::errc::io_error);
    }

    return status;
}

std::pair<boost::system::error_code, ByteArray>
    MCTPImpl::sendReceiveBlocked(DeviceID devID, const ByteArray& request,
                                 std::chrono::milliseconds timeout)
{
    auto receiveResult = std::make_pair(
        boost::system::errc::make_error_code(boost::system::errc::success),
        ByteArray());
    auto it = this->endpointMap.find(devID);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "SendReceiveBlocked: Eid not found in end point map",
            phosphor::logging::entry("EID=%d", devID.id));
        receiveResult.first =
            boost::system::errc::make_error_code(boost::system::errc::io_error);
        return receiveResult;
    }

    auto msg = connection->new_method_call(
        it->second.c_str(), "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "SendReceiveMctpMessagePayload");

    msg.append(devID.mctpEID());
    msg.append(request);
    msg.append(static_cast<uint16_t>(timeout.count()));

    try
    {
        auto reply = connection->call(msg);
        reply.read(receiveResult.second);
    }
    catch (const sdbusplus::exception::SdBusError& sdbusError)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "SendReceiveBlocked: Error in method call ",
            phosphor::logging::entry("EID=%d", devID.id));
        receiveResult.first =
            boost::system::errc::make_error_code(boost::system::errc::io_error);
    }

    return receiveResult;
}

void MCTPImpl::sendAsync(const SendCallback& callback, const DeviceID devID,
                         const uint8_t msgTag, const bool tagOwner,
                         const ByteArray& request)
{
    auto it = this->endpointMap.find(devID);
    if (this->endpointMap.end() == it)
    {
        boost::system::error_code ec =
            boost::system::errc::make_error_code(boost::system::errc::io_error);
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "sendAsync: Eid not found in end point map",
            phosphor::logging::entry("EID=%d", devID.id));
        if (callback)
        {
            callback(ec, -1);
        }
        return;
    }
    if (useSocket)
    {
        auto socket = this->socketIntf.find(it->second);
        socket->second->sendAsync(callback, devID.mctpEID(), msgTag, tagOwner,
                                  request);
    }
    else
    {
        connection->async_method_call(
            callback, it->second, "/xyz/openbmc_project/mctp",
            "xyz.openbmc_project.MCTP.Base", "SendMctpMessagePayload",
            devID.mctpEID(), msgTag, tagOwner, request);
    }
}

std::pair<boost::system::error_code, int>
    MCTPImpl::sendYield(boost::asio::yield_context& yield, const DeviceID devID,
                        const uint8_t msgTag, const bool tagOwner,
                        const ByteArray& request)
{
    auto it = this->endpointMap.find(devID);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "sendYield: Eid not found in end point map",
            phosphor::logging::entry("EID=%d", devID.id));
        return std::make_pair(
            boost::system::errc::make_error_code(boost::system::errc::io_error),
            -1);
    }

    boost::system::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::success);
    int status = connection->yield_method_call<int>(
        yield, ec, it->second, "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "SendMctpMessagePayload",
        devID.mctpEID(), msgTag, tagOwner, request);

    return std::make_pair(ec, status);
}

void MCTPImpl::addToEidMap(boost::asio::yield_context yield,
                           const std::string& serviceName)
{
    std::vector<std::string> services;
    services.emplace_back(this->getReadableName(serviceName));
    buildMatchingEndpointMap(yield, services);
}

size_t MCTPImpl::eraseDevice(DeviceID extendedEID)
{
    return endpointMap.erase(extendedEID);
}

std::optional<std::string>
    MCTPImpl::getDeviceLocation(const DeviceID extendedEID)
{
    auto it = this->endpointMap.find(extendedEID);
    if (it == this->endpointMap.end())
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "getDeviceLocation: Eid not found in end point map",
            phosphor::logging::entry("EID=%d", extendedEID.id));
        return std::nullopt;
    }

    try
    {
        auto locationCode = readPropertyValue<std::string>(
            *connection, it->second,
            "/xyz/openbmc_project/mctp/device/" +
                std::to_string(extendedEID.mctpEID()),
            "xyz.openbmc_project.Inventory.Decorator.LocationCode",
            "LocationCode");
        return locationCode.empty() ? std::nullopt
                                    : std::make_optional(locationCode);
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("Error in getting Physical.Location property from " + it->second +
             ". " + e.what())
                .c_str());
        return std::nullopt;
    }
}

static eid_t readOwnEID(const std::string& serviceName,
                        sdbusplus::asio::connection& connection)
{
    static const std::string baseInterface = "xyz.openbmc_project.MCTP.Base";
    static const std::string eidProperty = "Eid";
    return readPropertyValue<eid_t>(connection, serviceName,
                                    "/xyz/openbmc_project/mctp", baseInterface,
                                    eidProperty);
}

void MCTPImpl::triggerGetOwnEID(const std::string& serviceName)
{
    if (!this->eidChangeCallback)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "GetOwnEIDs callback is empty while trying to trigger");
        return;
    }

    try
    {
        eid_t eid = readOwnEID(serviceName, *this->connection);
        if (eid == 0)
        {
            return;
        }
        OwnEIDChange evt;
        OwnEIDChange::EIDChangeData data;
        data.eid = eid;
        data.service = getReadableName(serviceName);
        evt.context = &data;
        this->eidChangeCallback(evt);
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::WARNING>(
            ("Wrapper: Error reading eid from " + serviceName + ". " + e.what())
                .c_str());
    }
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

    auto matchedBusesCopy = matchedBuses;
    for (const auto& service : matchedBusesCopy)
    {
        triggerGetOwnEID(service);
    }
}

uint8_t MCTPImpl::getNetworkID(const std::string& serviceName)
{
    auto it = this->networkIDCache.find(serviceName);
    if (it != this->networkIDCache.end())
    {
        return it->second;
    }

    try
    {
        auto networkID = readPropertyValue<NetworkID>(
            *this->connection, serviceName, "/xyz/openbmc_project/mctp",
            "xyz.openbmc_project.MCTP.Base", "NetworkID");
        this->networkIDCache.emplace(serviceName, networkID);
        return networkID;
    }
    catch (const std::exception&)
    {
        phosphor::logging::log<phosphor::logging::level::WARNING>(
            ("NetworkID property not found in " + serviceName +
             ". Assuming EIDs wont overlap")
                .c_str());
    }
    return 0;
}

DeviceID MCTPImpl::getDeviceIDFromPath(
    const sdbusplus::message::object_path& objectPath,
    const std::string& serviceName)
{
    try
    {
        auto slashLoc = objectPath.str.find_last_of('/');
        if (objectPath.str.npos == slashLoc || slashLoc < 2)
        {
            throw std::runtime_error("Invalid device path");
        }

        auto strEID = objectPath.str.substr(slashLoc + 1);
        auto networkID = getNetworkID(serviceName);
        return DeviceID(std::stoi(strEID), networkID);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error(std::string("Error getting eid from ") +
                                 objectPath.str + ". " + e.what());
    }
}

void MCTPImpl::listenForMCTPChanges()
{
    static const std::string rule =
        "type='signal',path='/xyz/openbmc_project/mctp'";

    this->mctpChangesWatch = std::make_unique<sdbusplus::bus::match::match>(
        *connection, rule,
        std::bind(&MCTPImpl::onMCTPEvent, this, std::placeholders::_1));

    phosphor::logging::log<phosphor::logging::level::INFO>(
        "Wrapper: Listening for all MCTP related signals");
}

void MCTPImpl::onNewService(const std::string& serviceName)
{
    phosphor::logging::log<phosphor::logging::level::INFO>(
        (std::string("New service ") + serviceName).c_str());
    matchedBuses.emplace(serviceName);
    registerResponder(serviceName);

    triggerGetOwnEID(serviceName);
}

void MCTPImpl::onNewEID(const std::string& serviceName, DeviceID extendedEID)
{
    this->endpointMap.emplace(extendedEID, serviceName);
    if (!this->networkChangeCallback)
    {
        return;
    }
    boost::asio::spawn(
        connection->get_io_context(),
        [this, extendedEID, serviceName](boost::asio::yield_context yield) {
            mctpw::Event event;
            event.eid = extendedEID.mctpEID();
            event.deviceId = extendedEID;
            event.type = mctpw::Event::EventType::deviceAdded;
            event.serviceName = this->getReadableName(serviceName);
            this->networkChangeCallback(this, event, yield);
        },
        {});
}

void MCTPImpl::onNewInterface(sdbusplus::message::message& msg)
{
    DictType<std::string, DictType<std::string, MctpPropertiesVariantType>>
        values;
    sdbusplus::message::object_path objectPath;

    msg.read(objectPath, values);
    phosphor::logging::log<phosphor::logging::level::DEBUG>(
        (std::string("Interface added on ") + objectPath.str).c_str());

    if (objectPath.str == "/xyz/openbmc_project/mctp")
    {
        // Interface added on base object. Means new service.
        if (values.end() !=
                values.find(mctpw::MCTPWrapper::bindingToInterface.at(
                    config.bindingType)) ||
            config.bindingType == mctpw::BindingType::mctpOverAny)
        {
            this->onNewService(msg.get_sender());
        }
        return;
    }

    if (!matchedBuses.contains(msg.get_sender()))
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            (std::string("Ignoring service not in interset: ") +
             msg.get_sender())
                .c_str());
        return;
    }

    // TODO Check for /xyz/openbmc_project/mctp/\d+ using regex
    if (objectPath.str.starts_with("/xyz/openbmc_project/mctp/"))
    {
        auto newExtendedEID = getDeviceIDFromPath(objectPath, msg.get_sender());
        // Interface added on base endpoint object. Means new EID
        auto itSupportedMsgTypes =
            values.find("xyz.openbmc_project.MCTP.SupportedMessageTypes");
        if (values.end() != itSupportedMsgTypes &&
            config.type != MessageType::vdpci)
        {
            const auto& properties = itSupportedMsgTypes->second;
            const auto& registeredMsgType = properties.at(
                mctpw::MCTPImpl::msgTypeToPropertyName.at(config.type));
            if (std::get<bool>(registeredMsgType) ||
                config.type == mctpw::MessageType::any)
            {
                this->onNewEID(msg.get_sender(), newExtendedEID);
                return;
            }
        }
        auto itPCIVDM =
            values.find("xyz.openbmc_project.MCTP.PCIVendorDefined");
        if (config.type == MessageType::vdpci && itPCIVDM != values.end())
        {
            if (config.vendorId)
            {
                std::string vendorIdStr =
                    std::get<std::string>(itPCIVDM->second.at("VendorID"));
                uint16_t vendorId =
                    static_cast<uint16_t>(std::stoi(vendorIdStr, nullptr, 16));
                if (vendorId != be16toh(*config.vendorId))
                {
                    phosphor::logging::log<phosphor::logging::level::INFO>(
                        ("VendorID not matching for " + objectPath.str)
                            .c_str());
                    return;
                }

                if (config.vendorMessageType)
                {
                    std::vector<uint16_t> msgTypes =
                        std::get<std::vector<uint16_t>>(
                            itPCIVDM->second.at("MessageTypeProperty"));
                    auto itMsgType =
                        std::find(msgTypes.begin(), msgTypes.end(),
                                  be16toh(config.vendorMessageType->value));
                    if (msgTypes.end() == itMsgType)
                    {
                        phosphor::logging::log<phosphor::logging::level::INFO>(
                            ("Vendor Message Type not matching for " +
                             objectPath.str)
                                .c_str());
                        return;
                    }
                }
            }
            this->onNewEID(msg.get_sender(), newExtendedEID);
            return;
        }
    }
}

void MCTPImpl::onEIDRemoved(const std::string& serviceName, DeviceID deviceID)
{
    if (eraseDevice(deviceID) == 0)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            ("Removed device is not in endpoint map " +
             std::to_string(deviceID.mctpEID()))
                .c_str());
        return;
    }
    if (!this->networkChangeCallback)
    {
        return;
    }
    boost::asio::spawn(
        connection->get_io_context(),
        [this, deviceID, serviceName](boost::asio::yield_context yield) {
            mctpw::Event event;
            event.type = mctpw::Event::EventType::deviceRemoved;
            event.eid = deviceID.mctpEID();
            event.deviceId = deviceID;
            event.serviceName = this->getReadableName(serviceName);
            this->networkChangeCallback(this, event, yield);
        },
        {});
}

void MCTPImpl::onInterfaceRemoved(sdbusplus::message::message& msg)
{
    sdbusplus::message::object_path objectPath;
    std::vector<std::string> interfaces;
    msg.read(objectPath, interfaces);

    if (objectPath.str.starts_with("/xyz/openbmc_project/mctp/"))
    {
        if (std::find(interfaces.begin(), interfaces.end(),
                      "xyz.openbmc_project.MCTP.SupportedMessageTypes") !=
            interfaces.end())
        {
            try
            {
                auto deviceID =
                    getDeviceIDFromPath(objectPath, msg.get_sender());
                // Cannot check values of the interface since its removed
                this->onEIDRemoved(msg.get_sender(), deviceID);
            }
            catch (const std::exception& e)
            {

                phosphor::logging::log<phosphor::logging::level::INFO>(
                    (std::string("Error in eid remove callback ") + e.what())
                        .c_str());
            }
        }
    }
    else if (objectPath.str == "/xyz/openbmc_project/mctp")
    {
        if (std::find(interfaces.begin(), interfaces.end(),
                      "xyz.openbmc_project.MCTP.Base") != interfaces.end())
        {
            phosphor::logging::log<phosphor::logging::level::INFO>(
                ("Removing mctp service " + std::string(msg.get_sender()))
                    .c_str());
            this->matchedBuses.erase(msg.get_sender());
            this->uniqueNameToReadableCache.erase(msg.get_sender());
            for (auto& [eid, service] : this->endpointMap)
            {
                if (service == msg.get_sender())
                {
                    phosphor::logging::log<phosphor::logging::level::ERR>(
                        (std::string("EID entry invalid for : ") +
                         msg.get_sender())
                            .c_str());
                }
            }
        }
    }
}

void MCTPImpl::onMessageReceived(sdbusplus::message::message& msg)
{
    if (!this->receiveCallback && !this->extReceiveCallback)
    {
        return;
    }

    uint8_t messageType = 0;
    uint8_t srcEid = 0;
    uint8_t msgTag = 0;
    bool tagOwner = false;
    std::vector<uint8_t> payload;

    msg.read(messageType, srcEid, msgTag, tagOwner, payload);

    if (static_cast<MessageType>(messageType) != config.type)
    {
        return;
    }

    if (static_cast<MessageType>(messageType) == MessageType::vdpci)
    {
        struct VendorHeader
        {
            uint8_t vdpciMessageType;
            uint16_t vendorId;
            uint16_t intelVendorMessageId;
        } __attribute__((packed));
        VendorHeader* vendorHdr =
            reinterpret_cast<VendorHeader*>(payload.data());

        if (!config.vendorId || !config.vendorMessageType ||
            (vendorHdr->vendorId != config.vendorId) ||
            ((vendorHdr->intelVendorMessageId &
              config.vendorMessageType->mask) !=
             (config.vendorMessageType->value &
              config.vendorMessageType->mask)))
        {
            return;
        }
    }
    if (this->receiveCallback)
    {
        this->receiveCallback(this, srcEid, tagOwner, msgTag, payload, 0);
    }
    if (this->extReceiveCallback)
    {
        auto nwid = getNetworkID(msg.get_sender());
        this->extReceiveCallback(this, DeviceID(srcEid, nwid), tagOwner, msgTag,
                                 payload, 0);
    }
}

void MCTPImpl::onOwnEIDChange(std::string serviceName, eid_t eid)
{
    if (eid == 0)
    {
        return;
    }
    OwnEIDChange evt;
    OwnEIDChange::EIDChangeData data;
    data.eid = eid;
    serviceName = getReadableName(serviceName);
    data.service = std::move(serviceName);
    evt.context = &data;
    if (this->eidChangeCallback)
    {
        this->eidChangeCallback(evt);
    }
}

void MCTPImpl::onPropertiesChanged(sdbusplus::message::message& msg)
{
    std::string intfName;
    boost::container::flat_map<std::string, MctpPropertiesVariantType>
        propertiesChanged;

    msg.read(intfName, propertiesChanged);
    auto it = propertiesChanged.find("Eid");

    if (this->eidChangeCallback &&
        intfName == "xyz.openbmc_project.MCTP.Base" &&
        it != propertiesChanged.end())
    {
        this->onOwnEIDChange(msg.get_sender(), std::get<uint8_t>(it->second));
    }

    phosphor::logging::log<phosphor::logging::level::DEBUG>(
        (std::string("Property change on ") + intfName).c_str());
}

void MCTPImpl::onMCTPEvent(sdbusplus::message::message& msg)
{
    static const std::string intfAdded = "InterfacesAdded";
    static const std::string intfRemoved = "InterfacesRemoved";
    static const std::string msgReceived = "MessageReceivedSignal";
    static const std::string propChanged = "PropertiesChanged";

    phosphor::logging::log<phosphor::logging::level::DEBUG>(
        (std::string("MCTP general event from ") + msg.get_sender()).c_str());

    auto member = msg.get_member();
    if (member == intfAdded)
    {
        this->onNewInterface(msg);
    }

    if (!matchedBuses.contains(msg.get_sender()))
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            (std::string("Ignoring service not in interset: ") +
             msg.get_sender())
                .c_str());
        return;
    }

    if (member == intfRemoved)
    {
        this->onInterfaceRemoved(msg);
    }
    else if (member == msgReceived)
    {
        this->onMessageReceived(msg);
    }
    else if (member == propChanged)
    {
        this->onPropertiesChanged(msg);
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
    HandshakeCallback initiateHandshakeCallback, DeviceID devID)
{
    auto it = this->endpointMap.find(devID);

    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "initiateSPDMHandshake: Device ID not found in endpoint map",
            phosphor::logging::entry("EID=%d", devID.id));

        boost::system::error_code ec =
            boost::system::errc::make_error_code(boost::system::errc::io_error);

        if (initiateHandshakeCallback)
        {
            initiateHandshakeCallback(ec);
        }
        return;
    }

    connection->async_method_call(
        initiateHandshakeCallback, it->second, "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "InitiateHandshake", devID.id);
}

MCTPImpl::MCTPImpl(boost::asio::io_context& ioContext,
                   const MCTPConfiguration& configIn,
                   const ReconfigurationCallback& networkChangeCb,
                   const ReceiveMessageCallback& rxCb) :
    connection(std::make_shared<sdbusplus::asio::connection>(ioContext)),
    config(configIn), networkChangeCallback(networkChangeCb),
    receiveCallback(rxCb)
{
}

MCTPImpl::MCTPImpl(std::shared_ptr<sdbusplus::asio::connection> conn,
                   const MCTPConfiguration& configIn,

                   const ReconfigurationCallback& networkChangeCb,
                   const ReceiveMessageCallback& rxCb) :
    connection(conn), config(configIn), networkChangeCallback(networkChangeCb),
    receiveCallback(rxCb)
{
}

std::string MCTPImpl::getReadableName(const std::string& uniqueServiceName)
{
    boost::system::error_code ec;
    std::vector<std::pair<unsigned, std::string>> buses;
    DictType<std::string, std::vector<std::string>> services;
    std::vector<std::string> interfaces;

    if (uniqueServiceName.front() != ':')
    {
        return uniqueServiceName;
    }
    auto it = uniqueNameToReadableCache.find(uniqueServiceName);
    if (it != uniqueNameToReadableCache.end())
    {
        return it->second;
    }

    try
    {
        sdbusplus::message_t msg = connection->new_method_call(
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject");
        msg.append("/xyz/openbmc_project/mctp");
        msg.append(interfaces);
        auto reply = msg.call();
        reply.read(services);

        for (const auto& [serviceName, interfaces] : services)
        {
            std::string uniqueName;
            msg = connection->new_method_call(
                "org.freedesktop.DBus", "/org/freedesktop/DBus",
                "org.freedesktop.DBus", "GetNameOwner");
            msg.append(serviceName.c_str());
            reply = msg.call();
            reply.read(uniqueName);
            if (uniqueName == uniqueServiceName)
            {
                uniqueNameToReadableCache.emplace(uniqueServiceName,
                                                  serviceName);
                return serviceName;
            }
        }
    }
    catch (std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::INFO>(
            (std::string("getReadableName ") + e.what()).c_str());
    }
    return uniqueServiceName;
}

} // namespace mctpw
