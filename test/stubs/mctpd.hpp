#pragma once

#include "dbus_server.hpp"
#include "mctp_constants.hpp"

#include <boost/asio.hpp>
#include <boost/container/flat_map.hpp>
#include <format>
#include <memory>
#include <optional>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <unordered_map>
#include <vector>

using MctpPropertiesVariantT =
    std::variant<uint16_t, int16_t, int32_t, uint32_t, bool, std::string,
                 uint8_t, std::vector<uint8_t>, std::vector<uint16_t>>;

template <typename T1, typename T2>
using DictType = boost::container::flat_map<T1, T2>;

class MCTPService : public SDBusServer
{
  public:
    using ManagedObjectsT = DictType<
        sdbusplus::message::object_path,
        DictType<std::string, DictType<std::string, MctpPropertiesVariantT>>>;
    MCTPService(std::string name) : SDBusServer(name)
    {
    }
    virtual void task() override
    {
        init();
        addAllEIDS();
        io.run();
    }
    void init() override
    {
        setObjManagerPath("/xyz/openbmc_project/mctp");
        SDBusServer::init();
        baseIntf = objectServer->add_unique_interface(
            "/xyz/openbmc_project/mctp", "xyz.openbmc_project.MCTP.Base");

        baseIntf->register_method(
            "SendReceiveMctpMessagePayload",
            [this](uint8_t, std::vector<uint8_t> payload, uint16_t) {
                uint8_t firstElement = payload.front();

                std::vector<uint8_t> response = {2};
                if (firstElement != 0)
                {
                    return response;
                }
                throw std::runtime_error(
                    std::string("Simulated error for SendReceive payload"));
            });

        baseIntf->register_method(
            "SendMctpMessagePayload",
            [this](uint8_t, uint8_t, bool, std::vector<uint8_t> payload) {
                uint8_t firstElement = payload.front();
                if (firstElement != 0)
                {
                    return 0;
                }
                throw std::runtime_error(
                    std::string("Simulated error for send payload "));
            });

        baseIntf->register_method(
            "RegisterResponder",
            [this](uint8_t, std::vector<uint8_t> version) -> bool {
                uint8_t firstElement = version.front();
                if (firstElement != 0)
                {
                    return true;
                }
                throw std::runtime_error(std::string("Simulated error"));
            });

        baseIntf->register_method(
            "RegisterVdpciResponder",
            [this](uint16_t, uint16_t, std::vector<uint8_t> version) -> bool {
                uint8_t firstElement = version.front();
                if (firstElement != 0)
                {
                    return true;
                }
                throw std::runtime_error(std::string("Simulated error "));
            });

        baseIntf->register_method("TriggerDeviceDiscovery", [this]() {});

        baseIntf->register_method(
            "ReserveBandwidth",
            [this](boost::asio::yield_context, uint8_t, const uint16_t) {
                static uint8_t count = 0;
                count++;
                if (count == 1)
                {
                    return 0;
                }
                else if (count == 2)
                {
                    return -1;
                }
                throw std::runtime_error(
                    std::string("Simulated error for ReserveBandwidth"));
            });

        baseIntf->register_method(
            "ReleaseBandwidth", [this](boost::asio::yield_context, uint8_t) {
                static uint8_t count = 0;
                count++;
                if (count == 1)
                {
                    return 0;
                }
                else if (count == 2)
                {
                    return -1;
                }
                throw std::runtime_error(
                    std::string("Simulated error for ReleaseBandwidth "));
            });

        baseIntf->initialize();
    }
    void addEID(uint8_t eid, uint8_t supportedMsgTypeMask,
                std::optional<std::vector<uint16_t>> vdmTypes = std::nullopt)
    {
        eidsToAdd[eid] = std::make_tuple(supportedMsgTypeMask, vdmTypes);
    }

  protected:
    void addAllEIDS()
    {
        for (const auto& [eid, args] : eidsToAdd)
        {
            auto [msgType, vdmTypes] = args;
            addEIDToDbus(eid, msgType, vdmTypes);
        }
    }
    void addEIDToDbus(
        uint8_t eid, uint8_t supportedMsgTypeMask,
        std::optional<std::vector<uint16_t>> vdmTypes = std::nullopt)
    {
        auto path = std::format("/xyz/openbmc_project/mctp/device/{}",
                                static_cast<int>(eid));
        auto eidIntf = objectServer->add_unique_interface(
            path, "xyz.openbmc_project.MCTP.SupportedMessageTypes");
        eidIntf->register_property<bool>("MctpControl",
                                         (supportedMsgTypeMask & 0x01) != 0);
        eidIntf->register_property<bool>("PLDM",
                                         (supportedMsgTypeMask & 0x02) != 0);
        eidIntf->initialize();
        eidIntfMap[eid].emplace_back(std::move(eidIntf));
        if (vdmTypes)
        {
            const std::string_view pcieVdm =
                "xyz.openbmc_project.MCTP.PCIVendorDefined";
            auto pcieIntf =
                objectServer->add_unique_interface(path, pcieVdm.data());
            pcieIntf->register_property<std::string>("VendorID", "0x8086");
            pcieIntf->register_property<std::vector<uint16_t>>(
                "MessageTypeProperty", *vdmTypes);
            pcieIntf->initialize();
            eidIntfMap[eid].emplace_back(std::move(pcieIntf));
        }

        const std::string_view locationdCodeIntfName =
            "xyz.openbmc_project.Inventory.Decorator.LocationCode";
        auto locationCodeIntf = objectServer->add_unique_interface(
            path, locationdCodeIntfName.data());
        locationCodeIntf->register_property<std::string>("LocationCode",
                                                         "PCIe_Slot_1");
        locationCodeIntf->initialize();
        eidIntfMap[eid].emplace_back(std::move(locationCodeIntf));

        auto epIntf = objectServer->add_unique_interface(
            path, "xyz.openbmc_project.MCTP.Endpoint");
        epIntf->initialize();
        eidIntfMap[eid].emplace_back(std::move(epIntf));
    }

  protected:
    std::unique_ptr<sdbusplus::asio::dbus_interface> baseIntf;
    std::unordered_map<
        uint8_t, std::tuple<uint8_t, std::optional<std::vector<uint16_t>>>>
        eidsToAdd;
    std::unordered_map<
        uint8_t, std::vector<std::unique_ptr<sdbusplus::asio::dbus_interface>>>
        eidIntfMap;
};

class MCTPSMBus : public MCTPService
{
  public:
    MCTPSMBus() : MCTPService(service_names::mctpSMBus().data())
    {
    }
    void init() override
    {
        MCTPService::init();
        smbusIntf = objectServer->add_unique_interface(
            "/xyz/openbmc_project/mctp",
            "xyz.openbmc_project.MCTP.Binding.SMBus");
        smbusIntf->register_property<std::string>("BusPath", "/dev/i2c-4");
        smbusIntf->initialize();
    }

  protected:
    std::unique_ptr<sdbusplus::asio::dbus_interface> smbusIntf;
};
