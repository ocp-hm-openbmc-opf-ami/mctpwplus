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

#include "dbus_cb.hpp"

#include "mctp_impl.hpp"

#include <boost/container/flat_map.hpp>
#include <phosphor-logging/log.hpp>
#include <unordered_set>

template <typename T1, typename T2>
using DictType = boost::container::flat_map<T1, T2>;
using MctpPropertiesVariantType =
    std::variant<uint16_t, int16_t, int32_t, uint32_t, bool, std::string,
                 uint8_t, std::vector<uint8_t>>;

namespace mctpw
{

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

// TODO: Create utils module and move common functions to that
static NetworkID getNetworkId(sdbusplus::bus::bus& bus,
                              const std::string& serviceName)
{
    try
    {

        return readPropertyValue<NetworkID>(
            bus, serviceName, "/xyz/openbmc_project/mctp",
            "xyz.openbmc_project.MCTP.Base", "NetworkID");
    }
    catch (const std::exception&)
    {
        phosphor::logging::log<phosphor::logging::level::WARNING>(
            ("NetworkId property not found in " + serviceName +
             ". Assuming EIDs wont overlap")
                .c_str());
    }
    return 0;
}

int onPropertiesChanged(sd_bus_message* rawMsg, void* userData,
                        sd_bus_error* retError)
{
    if (!userData || (retError && sd_bus_error_is_set(retError)))
    {
        return -1;
    }

    MCTPImpl* context = static_cast<MCTPImpl*>(userData);
    sdbusplus::message::message message{rawMsg};
    if (!context->networkChangeCallback)
    {
        return -1;
    }
    /* Signal format:
     * STRING interface_name,
     * DICT<STRING,VARIANT> changed_properties,
     * ARRAY<STRING> invalidated_properties
     */
    DictType<std::string, MctpPropertiesVariantType> properties;
    std::string interface;
    message.read(interface, properties);
    static const std::unordered_set<std::string> tracedProperties = {
        "Eid",          "EidPool", "Mode", "NetworkId", "discoveredFlag",
        "SlaveAddress", "BusPath"};
    for (const auto& [propertyName, propertyVal] : properties)
    {
        if (tracedProperties.find(propertyName) != tracedProperties.end())
        {
            // TODO Handle property changed event
        }
    }
    return 1;
}

static LocalEID
    getEIdFromPath(const sdbusplus::message::object_path& object_path)
{
    try
    {
        auto slashLoc = object_path.str.find_last_of('/');
        if (object_path.str.npos == slashLoc)
        {
            throw std::runtime_error("Invalid device path");
        }
        auto strDeviceId = object_path.str.substr(slashLoc + 1);
        return static_cast<LocalEID>(std::stoi(strDeviceId));
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error(std::string("Error getting eid from ") +
                                 object_path.str + ". " + e.what());
    }
}

int onInterfacesAdded(sd_bus_message* rawMsg, void* userData,
                      sd_bus_error* retError)
{
    if (!userData || (retError && sd_bus_error_is_set(retError)))
    {
        return -1;
    }

    MCTPImpl* context = static_cast<MCTPImpl*>(userData);
    sdbusplus::message::message message{rawMsg};
    if (!context->networkChangeCallback)
    {
        return -1;
    }

    /* Signal format:
     * OBJPATH object_path,
     * DICT<STRING,DICT<STRING,VARIANT>> interfaces_and_properties
     */
    DictType<std::string, DictType<std::string, MctpPropertiesVariantType>>
        values;
    sdbusplus::message::object_path object_path;
    mctpw::Event event;
    try
    {
        message.read(object_path, values);
        auto serviceName = message.get_sender();
        auto itSupportedMsgTypes =
            values.find("xyz.openbmc_project.MCTP.SupportedMessageTypes");
        if (values.end() != itSupportedMsgTypes)
        {
            event.deviceId =
                DeviceID(getEIdFromPath(object_path),
                         getNetworkId(*context->connection, serviceName));
            event.eid = getEIdFromPath(object_path);
            const auto& properties = itSupportedMsgTypes->second;
            const auto& registeredMsgType =
                properties.at(mctpw::MCTPImpl::msgTypeToPropertyName.at(
                    context->config.type));
            if (std::get<bool>(registeredMsgType))
            {
                event.type = mctpw::Event::EventType::deviceAdded;
                boost::asio::spawn(context->connection->get_io_context(),
                                   [context, object_path, serviceName, userData,
                                    event](boost::asio::yield_context yield) {
                                       context->addToEidMap(yield, serviceName);
                                       context->networkChangeCallback(
                                           userData, event, yield);
                                       return 1;
                                   });
            }
        }
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            (std::string("onInterfaceAdded: ") + e.what()).c_str());
    }
    return -1;
}

int onInterfacesRemoved(sd_bus_message* rawMsg, void* userData,
                        sd_bus_error* retError)
{
    if (!userData || (retError && sd_bus_error_is_set(retError)))
    {
        return -1;
    }

    MCTPImpl* context = static_cast<MCTPImpl*>(userData);
    sdbusplus::message::message message{rawMsg};
    if (!context->networkChangeCallback)
    {
        return -1;
    }
    // MESSAGE "oas"
    try
    {
        sdbusplus::message::object_path object_path;
        std::vector<std::string> interfaces;
        message.read(object_path, interfaces);
        mctpw::Event event;
        // TODO Remove match rules for the services going down
        if (std::find(interfaces.begin(), interfaces.end(),
                      "xyz.openbmc_project.MCTP.SupportedMessageTypes") !=
            interfaces.end())
        {
            event.type = mctpw::Event::EventType::deviceRemoved;
            event.deviceId = DeviceID(
                getEIdFromPath(object_path),
                getNetworkId(*context->connection, message.get_sender()));
            event.eid = getEIdFromPath(object_path);

            if (context->eraseDevice(event.deviceId) == 1)
            {
                boost::asio::spawn(context->connection->get_io_context(),
                                   [context, userData,
                                    event](boost::asio::yield_context yield) {
                                       context->networkChangeCallback(
                                           userData, event, yield);
                                       return 1;
                                   });
            }
            else
            {
                phosphor::logging::log<phosphor::logging::level::ERR>(
                    "Removed device is not in endpoint map");
            }
        }
    }
    catch (const std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            (std::string("onInterfacesRemoved: ") + e.what()).c_str());
    }
    return -1;
}

int onMessageReceivedSignal(sd_bus_message* rawMsg, void* userData,
                            sd_bus_error* retError)
{
    if (!userData || (retError && sd_bus_error_is_set(retError)))
    {
        return -1;
    }

    try
    {
        MCTPImpl* context = static_cast<MCTPImpl*>(userData);
        sdbusplus::message::message message{rawMsg};

        if (!context->receiveCallback)
        {
            return -1;
        }
        uint8_t messageType = 0;
        uint8_t srcEid = 0;
        uint8_t msgTag = 0;
        bool tagOwner = false;
        std::vector<uint8_t> payload;

        message.read(messageType, srcEid, msgTag, tagOwner, payload);

        if (static_cast<MessageType>(messageType) != context->config.type)
        {
            return -1;
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

            if (!context->config.vendorId ||
                !context->config.vendorMessageType ||
                (vendorHdr->vendorId != context->config.vendorId) ||
                ((vendorHdr->intelVendorMessageId &
                  context->config.vendorMessageType->mask) !=
                 (context->config.vendorMessageType->value &
                  context->config.vendorMessageType->mask)))
            {
                return -1;
            }
        }
        DeviceID eeid(srcEid,
                      getNetworkId(*context->connection, message.get_sender()));
        context->receiveCallback(context, srcEid, tagOwner, msgTag, payload, 0);
        return 1;
    }
    catch (std::exception& e)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            (std::string("onMessageReceivedSignal: ") + e.what()).c_str());
    }

    return -1;
}

} // namespace mctpw
