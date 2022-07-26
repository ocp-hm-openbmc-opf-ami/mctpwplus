/*
// Copyright (c) 2022 Intel Corporation
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

#include <boost/asio.hpp>
#include <iostream>
#include <phosphor-logging/log.hpp>

int main(int argc, char* argv[])
{
    uint8_t eid = 10;
    using namespace mctpw;
    boost::asio::io_context io;
    auto conn = std::make_shared<sdbusplus::asio::connection>(io);
    MCTPConfiguration smbusConfig(mctpw::MessageType::pldm,
                                  mctpw::BindingType::mctpOverSmBus);
    MCTPWrapper mctpWrapper1(conn, smbusConfig, nullptr, nullptr);
    MCTPConfiguration vdpciConfig(mctpw::MessageType::vdpci,
                                  mctpw::BindingType::mctpOverSmBus, 0x8086,
                                  0xFFFF, 0xFF00);
    MCTPWrapper mctpWrapper2(conn, vdpciConfig, nullptr, nullptr);

    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait(
        [&io](const boost::system::error_code&, const int&) { io.stop(); });

    boost::asio::spawn(io, [eid,
                            &mctpWrapper1](boost::asio::yield_context yield) {
        mctpWrapper1.detectMctpEndpoints(yield);
        std::cout << "Registering a SMBus PLDM responder" << std::endl;
        VersionFields specVersion = {0xF1, 0xF1, 0xF0, 0};
        auto rcvStatus = mctpWrapper1.registerResponder(specVersion);
        std::cout << ((rcvStatus == boost::system::errc::success) ? "Success"
                                                                  : "Failed")
                  << '\n';
    });

    boost::asio::spawn(io, [eid, &mctpWrapper2,
                            &conn](boost::asio::yield_context yield) {
        mctpWrapper2.detectMctpEndpoints(yield);
        std::cout << "Registering a SMBus VDPCI responder" << std::endl;
        VersionFields v1 = {0xF1, 0xF1, 0xF0, 0};
        VersionFields v2 = {0xF1, 0xF1, 0xF0, 0};
        std::vector<VersionFields> versions = {v1, v2};
        auto rcvStatus = mctpWrapper2.registerResponder(versions);
        std::cout << ((rcvStatus == boost::system::errc::success) ? "Success"
                                                                  : "Failed")
                  << '\n';

        constexpr uint16_t intelId = 0x8086;
        constexpr uint16_t vdMsgType = 0x1234;
        constexpr uint16_t vdMsgMask = 0x0F0F;
        MCTPConfiguration config(mctpw::MessageType::vdpci,
                                 mctpw::BindingType::mctpOverSmBus, intelId,
                                 vdMsgType, vdMsgMask);
        MCTPWrapper mctpWrapper(conn, config, nullptr, nullptr);
        rcvStatus = mctpWrapper.registerResponder(versions);
        std::cout << "Registering VDPCI 0x1234"
                  << ((rcvStatus == boost::system::errc::success) ? "Success"
                                                                  : "Failed")
                  << '\n';
    });

    io.run();
    return 0;
}
