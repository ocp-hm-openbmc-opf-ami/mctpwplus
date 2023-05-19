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
                 uint8_t, std::vector<uint8_t>>;

template <typename Property>
static auto
    readPropertyValue(sdbusplus::bus::bus& bus, const std::string& service,
                      const std::string& path, const std::string& interface,
                      const std::string& property)
{
    phosphor::logging::log<phosphor::logging::level::DEBUG>(
        (std::string("Reading ") + service + " " + path + " " + interface +
         " " + property)
            .c_str());
    auto msg = bus.new_method_call(service.c_str(), path.c_str(),
                                   "org.freedesktop.DBus.Properties", "Get");

    msg.append(interface.c_str(), property.c_str());
    auto reply = bus.call(msg);

    std::variant<Property> v;
    reply.read(v);
    return std::get<Property>(v);
}

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
                       });
}

void MCTPImpl::triggerMCTPDeviceDiscovery(const eid_t dstEId)
{
    auto it = this->endpointMap.find(dstEId);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "triggerMCTPDeviceDiscovery: EID not found in end point map",
            phosphor::logging::entry("EID=%d", dstEId));
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
        it->second.second, "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "TriggerDeviceDiscovery");
}

int MCTPImpl::reserveBandwidth(boost::asio::yield_context yield,
                               const eid_t dstEId, const uint16_t timeout)
{
    auto it = this->endpointMap.find(dstEId);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("reserveBandwidth: EID not found in end point map" +
             std::to_string(dstEId))
                .c_str());
        return -1;
    }
    boost::system::error_code ec;
    int status = connection->yield_method_call<int>(
        yield, ec, it->second.second, "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "ReserveBandwidth", dstEId, timeout);

    if (ec)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("ReserveBandwidth: failed for EID: " + std::to_string(dstEId) +
             " " + ec.message())
                .c_str());
        return -1;
    }
    else if (status < 0)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("ReserveBandwidth: failed for EID: " + std::to_string(dstEId) +
             " rc: " + std::to_string(status))
                .c_str());
    }
    return status;
}

int MCTPImpl::releaseBandwidth(boost::asio::yield_context yield,
                               const eid_t dstEId)
{
    auto it = this->endpointMap.find(dstEId);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("ReleaseBandwidth: EID not found in end point map" +
             std::to_string(dstEId))
                .c_str());
        return -1;
    }
    boost::system::error_code ec;
    int status = connection->yield_method_call<int>(
        yield, ec, it->second.second, "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "ReleaseBandwidth", dstEId);
    if (ec)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("ReleaseBandwidth: failed for EID: " + std::to_string(dstEId) +
             " " + ec.message())
                .c_str());
        return -1;
    }
    else if (status < 0)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("ReleaseBandwidth: failed for EID: " + std::to_string(dstEId) +
             " rc: " + std::to_string(status))
                .c_str());
    }

    return status;
}

boost::system::error_code
    MCTPImpl::detectMctpEndpoints(boost::asio::yield_context yield)
{
    phosphor::logging::log<phosphor::logging::level::DEBUG>(
        "Detecting mctp endpoints");

    boost::system::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::success);
    auto bus_vector = findBusByBindingType(yield);
    if (bus_vector)
    {
        endpointMap = buildMatchingEndpointMap(yield, bus_vector.value());
    }

    if (this->eidChangeCallback)
    {
        // getOwnEID was called before. Retrigger the events
        this->getOwnEIDs(this->eidChangeCallback);
    }

    listenForMCTPChanges();

    phosphor::logging::log<phosphor::logging::level::DEBUG>(
        ("Detecting mctp endpoints completed. Found " +
         std::to_string(endpointMap.size()))
            .c_str());
    return ec;
}

int MCTPImpl::getBusId(const std::string& serviceName)
{
    // TODO - the bus ID parameter is unused in the library, this can be cleaned
    // up
    try
    {
        int bus = -1;
        static int i3cBusId = 0;
        if (config.bindingType == mctpw::BindingType::mctpOverSmBus)
        {
            std::string pv = readPropertyValue<std::string>(
                static_cast<sdbusplus::bus::bus&>(*connection), serviceName,
                "/xyz/openbmc_project/mctp",
                mctpw::MCTPWrapper::bindingToInterface.at(config.bindingType),
                "BusPath");
            // sample buspath like /dev/i2c-2
            /* format of BusPath:path-bus */
            std::vector<std::string> splitted;
            boost::split(splitted, pv, boost::is_any_of("-"));
            if (splitted.size() == 2)
            {
                try
                {
                    bus = std::stoi(splitted[1]);
                }
                catch (std::exception& e)
                {
                    throw std::runtime_error(
                        std::string("Invalid buspath on ") + pv);
                }
            }
        }
        else if (config.bindingType == mctpw::BindingType::mctpOverPcieVdm)
        {
            bus = readPropertyValue<uint16_t>(
                static_cast<sdbusplus::bus::bus&>(*connection), serviceName,
                "/xyz/openbmc_project/mctp",
                mctpw::MCTPWrapper::bindingToInterface.at(config.bindingType),
                "BDF");
        }
        else if (config.bindingType == mctpw::BindingType::mctpOverI3C)
        {
            bus = i3cBusId++;
        }
        else
        {
            throw std::invalid_argument("Unsupported binding type");
        }
        return bus;
    }
    catch (const std::exception& e)
    {
        throw boost::system::system_error(
            boost::system::errc::make_error_code(boost::system::errc::io_error),
            (std::string("Error in getting Bus property from ") + serviceName +
             ". " + e.what()));
    }
}

void MCTPImpl::addUniqueNameToMatchedServices(const std::string& serviceName,
                                              boost::asio::yield_context yield)
{
    boost::system::error_code ec;
    std::string uniqueName = connection->yield_method_call<std::string>(
        yield, ec, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetNameOwner", serviceName.c_str());

    if (ec)
    {
        std::string errMsg = std::string("GetUniqueName unsuccesful for ") +
                             serviceName + ". " + ec.message();
        phosphor::logging::log<phosphor::logging::level::WARNING>(
            errMsg.c_str());
        uniqueName = serviceName;
    }

    this->matchedBuses.emplace(uniqueName);
}

std::optional<std::vector<std::pair<unsigned, std::string>>>
    MCTPImpl::findBusByBindingType(boost::asio::yield_context yield)
{
    boost::system::error_code ec;
    std::vector<std::pair<unsigned, std::string>> buses;
    DictType<std::string, std::vector<std::string>> services;
    std::vector<std::string> interfaces;
    try
    {
        interfaces.push_back(
            mctpw::MCTPWrapper::bindingToInterface.at(config.bindingType));
        // find the services, with their interfaces, that implement a
        // certain object path
        services = connection->yield_method_call<decltype(services)>(
            yield, ec, "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject",
            "/xyz/openbmc_project/mctp", interfaces);

        if (ec)
        {
            throw std::runtime_error(
                (std::string("Error getting mctp services. ") + ec.message())
                    .c_str());
        }

        for (const auto& [service, intfs] : services)
        {
            try
            {
                int bus = this->getBusId(service);
                buses.emplace_back(bus, service);
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
MCTPImpl::EndpointMap MCTPImpl::buildMatchingEndpointMap(
    boost::asio::yield_context yield,
    std::vector<std::pair<unsigned, std::string>>& buses)
{
    std::unordered_map<uint8_t, std::pair<unsigned, std::string>> eids;
    for (auto& bus : buses)
    {
        boost::system::error_code ec;
        DictType<sdbusplus::message::object_path,
                 DictType<std::string,
                          DictType<std::string, MctpPropertiesVariantType>>>
            values;
        // get all objects, interfaces and properties in a single method
        // call DICT<OBJPATH,DICT<STRING,DICT<STRING,VARIANT>>>
        // objpath_interfaces_and_properties
        values = connection->yield_method_call<decltype(values)>(
            yield, ec, bus.second.c_str(), "/xyz/openbmc_project/mctp",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");

        if (ec)
        {
            phosphor::logging::log<phosphor::logging::level::WARNING>(
                (std::string("Error getting managed objects on ") + bus.second +
                 ". Bus " + std::to_string(bus.first))
                    .c_str());
            continue;
        }

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
                /*SupportedMessageTypes interface is mandatory*/
                auto& msgIf = interfaces.at(
                    "xyz.openbmc_project.MCTP.SupportedMessageTypes");
                MctpPropertiesVariantType pv;
                pv = msgIf.at(msgTypeToPropertyName.at(config.type));

                if (std::get<bool>(pv) == false)
                {
                    continue;
                }
                if (mctpw::MessageType::vdpci == config.type)
                {
                    if (config.vendorId)
                    {
                        static const char* vdMsgTypeInterface =
                            "xyz.openbmc_project.MCTP.PCIVendorDefined";
                        auto vendorIdStr = readPropertyValue<std::string>(
                            *connection, bus.second.c_str(), objectPath.str,
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
                                    *connection, bus.second.c_str(),
                                    objectPath.str, vdMsgTypeInterface,
                                    "MessageTypeProperty");
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
                    /* take the last element and convert it to eid */
                    uint8_t eid = static_cast<eid_t>(
                        std::stoi(splitted[splitted.size() - 1]));
                    eids[eid] = bus;
                }
            }
            catch (std::exception& e)
            {
                phosphor::logging::log<phosphor::logging::level::ERR>(e.what());
            }
        }
    }
    return eids;
}

void MCTPImpl::sendReceiveAsync(ReceiveCallback callback, eid_t dstEId,
                                const ByteArray& request,
                                std::chrono::milliseconds timeout)
{
    ByteArray response;
    auto it = this->endpointMap.find(dstEId);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "SendReceiveAsync: Eid not found in end point map",
            phosphor::logging::entry("EID=%d", dstEId));
        boost::system::error_code ec =
            boost::system::errc::make_error_code(boost::system::errc::io_error);
        if (callback)
        {
            callback(ec, response);
        }
        return;
    }

    connection->async_method_call(
        callback, it->second.second, "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "SendReceiveMctpMessagePayload",
        dstEId, request, static_cast<uint16_t>(timeout.count()));
}

std::pair<boost::system::error_code, ByteArray>
    MCTPImpl::sendReceiveYield(boost::asio::yield_context yield, eid_t dstEId,
                               const ByteArray& request,
                               std::chrono::milliseconds timeout)
{
    auto receiveResult = std::make_pair(
        boost::system::errc::make_error_code(boost::system::errc::success),
        ByteArray());
    auto it = this->endpointMap.find(dstEId);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "SendReceiveYield: Eid not found in end point map",
            phosphor::logging::entry("EID=%d", dstEId));
        receiveResult.first =
            boost::system::errc::make_error_code(boost::system::errc::io_error);
        return receiveResult;
    }
    receiveResult.second = connection->yield_method_call<ByteArray>(
        yield, receiveResult.first, it->second.second,
        "/xyz/openbmc_project/mctp", "xyz.openbmc_project.MCTP.Base",
        "SendReceiveMctpMessagePayload", dstEId, request,
        static_cast<uint16_t>(timeout.count()));

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
        if (reply.is_method_error())
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "D-Bus error in registering the responder");

            return boost::system::errc::make_error_code(
                boost::system::errc::io_error);
        }
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
    MCTPImpl::sendReceiveBlocked(eid_t dstEId, const ByteArray& request,
                                 std::chrono::milliseconds timeout)
{
    auto receiveResult = std::make_pair(
        boost::system::errc::make_error_code(boost::system::errc::success),
        ByteArray());
    auto it = this->endpointMap.find(dstEId);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "SendReceiveBlocked: Eid not found in end point map",
            phosphor::logging::entry("EID=%d", dstEId));
        receiveResult.first =
            boost::system::errc::make_error_code(boost::system::errc::io_error);
        return receiveResult;
    }

    auto msg = connection->new_method_call(
        it->second.second.c_str(), "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "SendReceiveMctpMessagePayload");

    msg.append(dstEId);
    msg.append(request);
    msg.append(static_cast<uint16_t>(timeout.count()));

    auto reply = connection->call(msg);
    if (reply.is_method_error())
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "SendReceiveBlocked: Error in method call ",
            phosphor::logging::entry("EID=%d", dstEId));
        receiveResult.first =
            boost::system::errc::make_error_code(boost::system::errc::io_error);
        return receiveResult;
    }
    reply.read(receiveResult.second);

    return receiveResult;
}

void MCTPImpl::sendAsync(const SendCallback& callback, const eid_t dstEId,
                         const uint8_t msgTag, const bool tagOwner,
                         const ByteArray& request)
{
    auto it = this->endpointMap.find(dstEId);
    if (this->endpointMap.end() == it)
    {
        boost::system::error_code ec =
            boost::system::errc::make_error_code(boost::system::errc::io_error);
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "sendAsync: Eid not found in end point map",
            phosphor::logging::entry("EID=%d", dstEId));
        if (callback)
        {
            callback(ec, -1);
        }
        return;
    }

    connection->async_method_call(
        callback, it->second.second, "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "SendMctpMessagePayload", dstEId,
        msgTag, tagOwner, request);
}

std::pair<boost::system::error_code, int>
    MCTPImpl::sendYield(boost::asio::yield_context& yield, const eid_t dstEId,
                        const uint8_t msgTag, const bool tagOwner,
                        const ByteArray& request)
{
    auto it = this->endpointMap.find(dstEId);
    if (this->endpointMap.end() == it)
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            "sendYield: Eid not found in end point map",
            phosphor::logging::entry("EID=%d", dstEId));
        return std::make_pair(
            boost::system::errc::make_error_code(boost::system::errc::io_error),
            -1);
    }

    boost::system::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::success);
    int status = connection->yield_method_call<int>(
        yield, ec, it->second.second, "/xyz/openbmc_project/mctp",
        "xyz.openbmc_project.MCTP.Base", "SendMctpMessagePayload", dstEId,
        msgTag, tagOwner, request);

    return std::make_pair(ec, status);
}

void MCTPImpl::addToEidMap(boost::asio::yield_context yield,
                           const std::string& serviceName)
{
    int busID = 0;
    try
    {
        busID = getBusId(serviceName);
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("Error in getting getBusID from service" + serviceName + ". " +
             e.what())
                .c_str());
        return;
    }
    std::vector<std::pair<unsigned, std::string>> buses;
    buses.emplace_back(busID, serviceName);
    auto eidMap = buildMatchingEndpointMap(yield, buses);
    this->endpointMap.insert(eidMap.begin(), eidMap.end());
}

size_t MCTPImpl::eraseDevice(eid_t eid)
{
    return endpointMap.erase(eid);
}

std::optional<std::string> MCTPImpl::getDeviceLocation(const eid_t eid)
{
    auto it = this->endpointMap.find(eid);
    if (it == this->endpointMap.end())
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "getDeviceLocation: Eid not found in end point map",
            phosphor::logging::entry("EID=%d", eid));
        return std::nullopt;
    }

    try
    {
        auto locationCode = readPropertyValue<std::string>(
            static_cast<sdbusplus::bus::bus&>(*connection), it->second.second,
            "/xyz/openbmc_project/mctp/device/" + std::to_string(eid),
            "xyz.openbmc_project.Inventory.Decorator.LocationCode",
            "LocationCode");
        return locationCode.empty() ? std::nullopt
                                    : std::make_optional(locationCode);
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            ("Error in getting Physical.Location property from " +
             it->second.second + ". " + e.what())
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
        OwnEIDChange evt;
        OwnEIDChange::EIDChangeData data;
        data.eid = eid;
        data.service = serviceName;
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

static eid_t getEIDFromPath(const sdbusplus::message::object_path& objectPath)
{
    try
    {
        auto slashLoc = objectPath.str.find_last_of('/');
        if (objectPath.str.npos == slashLoc)
        {
            throw std::runtime_error("Invalid device path");
        }
        auto strDeviceId = objectPath.str.substr(slashLoc + 1);
        return static_cast<eid_t>(std::stoi(strDeviceId));
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

void MCTPImpl::onNewEID(const std::string& serviceName, eid_t eid)
{
    if (!this->networkChangeCallback)
    {
        return;
    }
    this->endpointMap.emplace(eid, std::make_pair(0, serviceName));
    boost::asio::spawn(connection->get_io_context(),
                       [this, eid](boost::asio::yield_context yield) {
                           mctpw::Event event;
                           event.eid = eid;
                           event.type = mctpw::Event::EventType::deviceAdded;
                           this->networkChangeCallback(this, event, yield);
                       });
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
            values.find(
                mctpw::MCTPWrapper::bindingToInterface.at(config.bindingType)))
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

    if (objectPath.str.starts_with("/xyz/openbmc_project/mctp/device/"))
    {
        // Interface added on base endpoint object. Means new EID
        auto itSupportedMsgTypes =
            values.find("xyz.openbmc_project.MCTP.SupportedMessageTypes");
        if (values.end() != itSupportedMsgTypes)
        {
            auto newEid = getEIDFromPath(objectPath);
            const auto& properties = itSupportedMsgTypes->second;
            const auto& registeredMsgType = properties.at(
                mctpw::MCTPImpl::msgTypeToPropertyName.at(config.type));
            if (std::get<bool>(registeredMsgType))
            {
                this->onNewEID(msg.get_sender(), newEid);
            }
        }
    }
}

void MCTPImpl::onEIDRemoved(eid_t eid)
{
    if (eraseDevice(eid) == 1)
    {
        if (!this->networkChangeCallback)
        {
            return;
        }
        boost::asio::spawn(connection->get_io_context(),
                           [this, eid](boost::asio::yield_context yield) {
                               mctpw::Event event;
                               event.type =
                                   mctpw::Event::EventType::deviceRemoved;
                               event.eid = eid;
                               this->networkChangeCallback(this, event, yield);
                           });
    }
    else
    {
        phosphor::logging::log<phosphor::logging::level::DEBUG>(
            ("Removed device is not in endpoint map " + std::to_string(eid))
                .c_str());
    }
}

void MCTPImpl::onInterfaceRemoved(sdbusplus::message::message& msg)
{
    sdbusplus::message::object_path objectPath;
    std::vector<std::string> interfaces;
    msg.read(objectPath, interfaces);

    if (objectPath.parent_path() == "/xyz/openbmc_project/mctp/device")
    {
        if (std::find(interfaces.begin(), interfaces.end(),
                      "xyz.openbmc_project.MCTP.SupportedMessageTypes") !=
            interfaces.end())
        {
            auto eid = getEIDFromPath(objectPath);
            this->onEIDRemoved(eid);
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
            for (auto& [eid, service] : this->endpointMap)
            {
                if (service.second == msg.get_sender())
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
    if (!this->receiveCallback)
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
    this->receiveCallback(this, srcEid, tagOwner, msgTag, payload, 0);
}

void MCTPImpl::onOwnEIDChange(std::string serviceName, eid_t eid)
{
    OwnEIDChange evt;
    OwnEIDChange::EIDChangeData data;
    data.eid = eid;
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
    connection(conn),
    config(configIn), networkChangeCallback(networkChangeCb),
    receiveCallback(rxCb)
{
}
} // namespace mctpw
