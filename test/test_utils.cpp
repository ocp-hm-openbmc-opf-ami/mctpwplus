/*
// Copyright (c) 2024 Intel Corporation
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
#include "stubs/mctp_constants.hpp"
#include "stubs/stub_process.hpp"
#include "utils.hpp"

#include <gtest/gtest.h>

using namespace testing;
using namespace mctpw;

class TestUtils : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Set up any necessary resources for the test
        io = std::make_shared<boost::asio::io_context>();
        conn = std::make_shared<sdbusplus::asio::connection>(*io);
        service = service_names::mctpSMBus();
        objectPath = "/xyz/openbmc_project/mctp";
        interface = interfaces::mctpBase();
        method = "ReserveBandwidth";
    }

    void TearDown() override
    {
        // Clean up any resources used by the test
        conn.reset();
        io.reset();
    }

    std::shared_ptr<boost::asio::io_context> io;
    std::shared_ptr<sdbusplus::asio::connection> conn;
    std::string service;
    std::string objectPath;
    std::string interface;
    std::string method;
};

TEST_F(TestUtils, MethodCallWithReturn)
{
    uint8_t arg1 = 0x01;
    uint16_t arg2 = 0x02;
    bool cbCalled = false;

    boost::asio::spawn(
        *io,
        [this, arg1, arg2, &cbCalled](boost::asio::yield_context yield) {
            {
                auto status = mctpw::methodCall<int>(
                    *conn, service, objectPath, interface, method,
                    std::make_optional<boost::asio::yield_context>(yield),
                    arg2);
                EXPECT_FALSE(status.has_value());
                EXPECT_TRUE(status.error());
            }
            {
                auto status = mctpw::methodCall<int>(
                    *conn, service, objectPath, interface, method,
                    std::make_optional<boost::asio::yield_context>(yield), arg1,
                    arg2);
                EXPECT_TRUE(status.has_value());
                std::cout << "Status " << *status << '\n';
                EXPECT_EQ(*status, -1);
            }
            cbCalled = true;
            this->io->stop();
        },
        {});

    int status = mctpw::methodCall<int>(*conn, service, objectPath, interface,
                                        method, arg1, arg2);
    std::cout << "Status " << status << '\n';
    EXPECT_EQ(status, 0);

    io->run_for(std::chrono::seconds(15));
    EXPECT_TRUE(cbCalled);
}

TEST_F(TestUtils, MethodCallVoid)
{
    bool cbCalled = false;
    boost::asio::spawn(
        *io,
        [this, &cbCalled](boost::asio::yield_context yield) {
            {
                auto status = mctpw::methodCall(
                    *conn, service, objectPath, interface,
                    "TriggerDeviceDiscoveryInvalid",
                    std::make_optional<boost::asio::yield_context>(yield));
                EXPECT_TRUE(status);
            }
            {
                auto status = mctpw::methodCall(
                    *conn, service, objectPath, interface,
                    "TriggerDeviceDiscovery",
                    std::make_optional<boost::asio::yield_context>(yield));
                EXPECT_FALSE(status);
            }
            cbCalled = true;
            this->io->stop();
        },
        {});

    EXPECT_NO_THROW(mctpw::methodCall(*conn, service, objectPath, interface,
                                      "TriggerDeviceDiscovery"));

    io->run_for(std::chrono::seconds(15));
    EXPECT_TRUE(cbCalled);
}

TEST_F(TestUtils, ReadProperty)
{
    bool cbCalled = false;
    boost::asio::spawn(
        *io,
        [this, &cbCalled](boost::asio::yield_context yield) {
            {
                auto eid = mctpw::readPropertyValue<uint8_t>(
                    *conn, service, objectPath, interface, "Eid", yield);
                EXPECT_TRUE(eid.has_value());
                EXPECT_EQ(*eid, 4);
            }
            {
                auto eid = mctpw::readPropertyValue<uint8_t>(
                    *conn, service, objectPath, interface, "Eids", yield);
                EXPECT_FALSE(eid.has_value());
            }
            cbCalled = true;
            this->io->stop();
        },
        {});

    auto eid = mctpw::readPropertyValue<uint8_t>(*conn, service, objectPath,
                                                 interface, "Eid");
    EXPECT_EQ(eid, 4);

    io->run_for(std::chrono::seconds(15));
    EXPECT_TRUE(cbCalled);
}

int main(int argc, char** argv)
{
    StubProcesses proc;
    if (proc.isParentProcess())
    {
        ::testing::InitGoogleTest(&argc, argv);
        return RUN_ALL_TESTS();
    }
}
