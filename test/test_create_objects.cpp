#include "mctp_impl.hpp"
#include "mctp_wrapper.hpp"

#include <boost/asio.hpp>
#include <iostream>
#include <sdbusplus/asio/connection.hpp>

#include <gtest/gtest.h>

using namespace testing;
using namespace mctpw;

std::shared_ptr<boost::asio::io_context> io =
    std::make_shared<boost::asio::io_context>();

TEST(DeviceID, DeviceIDConstuctorTest)
{
    // All costructors of DeviceID
    constexpr uint8_t defaultEId = 9;
    eid_t eid = defaultEId;
    uint8_t networkId = defaultEId;
    mctpw::DeviceID deviceId(eid, networkId);

    mctpw::DeviceID devId2;
    EXPECT_NE(devId2, deviceId);

    eid = 10;
    mctpw::DeviceID device(eid, (networkId + 1));
    EXPECT_NE(device, deviceId);
    EXPECT_EQ(device.mctpEID(), eid);

    mctpw::DeviceID deviceID(eid, 0x34);
    EXPECT_NE(deviceID, deviceId);
    EXPECT_EQ(deviceID.networkId(), 0x34);

    std::unordered_map<DeviceID, std::string> testMap;
    DeviceID d1(9, 0);
    DeviceID d2(9, 1);
    testMap[d1] = "xyz";
    testMap[d2] = "abc";
    EXPECT_EQ(testMap.size(), 2);
    EXPECT_TRUE(d1 < d2);
}

TEST(MCTPConfiguration, MCTPConfigurationConstructorTest)
{
    // All 4 constructors of MCTPConfiguration
    using namespace mctpw;
    uint16_t vendorId = 0x33;
    uint16_t vendorMsgType = 0x01;
    uint16_t vendorMsgTypeMask = 0x10;
    uint16_t value = 0x1234;
    uint16_t mask = 0x00FF;

    // MCTPConfigurationWithNonVdpci
    {
        MCTPConfiguration config(mctpw::MessageType::vdpci,
                                 mctpw::BindingType::mctpOverSmBus, vendorId);
        EXPECT_THROW(MCTPConfiguration config(mctpw::MessageType::pldm,
                                              mctpw::BindingType::mctpOverSmBus,
                                              vendorId),
                     std::exception);
        EXPECT_NO_THROW(MCTPConfiguration config(
            mctpw::MessageType::vdpci, mctpw::BindingType::mctpOverSmBus,
            vendorId));
    }
    // MCTPConfigurationWithVdpci
    {
        EXPECT_NO_THROW(MCTPConfiguration config(
            mctpw::MessageType::vdpci, mctpw::BindingType::mctpOverSmBus,
            vendorId));
    }
    // MCTPConfigurationWithVdpciVendorMessage
    {
        EXPECT_NO_THROW(MCTPConfiguration config(
            mctpw::MessageType::vdpci, mctpw::BindingType::mctpOverSmBus,
            vendorId, vendorMsgType, vendorMsgTypeMask));
    }
    // MCTPConfigurationVendorIDVdpciVendorMesageType
    {
        vendorId = 0;
        vendorMsgType = 0;
        vendorMsgTypeMask = 0;
        EXPECT_NO_THROW(MCTPConfiguration config(
            MessageType::vdpci, BindingType::mctpOverSmBus, vendorId,
            vendorMsgType, vendorMsgTypeMask));
    }
    // VendorMessageType method test
    MCTPConfiguration::VendorMessageType msgType(value, mask);
    EXPECT_EQ(msgType.value, value);
    EXPECT_EQ(msgType.mask, mask);
    EXPECT_EQ(msgType.cmdSetType(), value & mask);
}

TEST(MCTPWrapper, ConstructorTest)
{
    // Two types of constructor for MCTPWrapper
    // ConstructorWithSDBusConnection
    std::shared_ptr<sdbusplus::asio::connection> conn;
    {
        mctpw::MCTPConfiguration config;
        EXPECT_NO_THROW(mctpw::MCTPWrapper mctpWrapper(conn, config));
    }
    // ConstructorWithIOContext
    {
        MCTPConfiguration config(mctpw::MessageType::vdpci,
                                 mctpw::BindingType::mctpOverSmBus);
        EXPECT_NO_THROW(MCTPWrapper mctpWrapper(*io, config, nullptr, nullptr));
    }
}
