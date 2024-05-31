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