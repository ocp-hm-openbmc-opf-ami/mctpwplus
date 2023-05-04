/*
// Copyright (c) 2023 Intel Corporation
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

#include "../mctp_wrapper.hpp"

#include <CLI/CLI.hpp>
#include <boost/asio.hpp>
#include <condition_variable>
#include <iostream>

using mctpw::BindingType;
using mctpw::MessageType;

int main(int argc, char* argv[])
{
    CLI::App app("MCTP Device Manager");
    uint16_t vendorId = 0;
    uint16_t vdmType = 0;
    uint16_t vdmMask = 0;
    mctpw::BindingType bindingType{};
    mctpw::MessageType msgType{};

    std::map<std::string, BindingType> bindingArgs{
        {"smbus", BindingType::mctpOverSmBus},
        {"pcie", BindingType::mctpOverPcieVdm},
        {"usb", BindingType::mctpOverUsb},
        {"kcs", BindingType::mctpOverKcs},
        {"serial", BindingType::mctpOverSerial},
        {"i3c", BindingType::mctpOverI3C},
        {"vendor", BindingType::vendorDefined}};

    std::map<std::string, MessageType> msgTypeArgs{
        {"pldm", MessageType::pldm},    {"ncsi", MessageType::ncsi},
        {"eth", MessageType::ethernet}, {"nvme", MessageType::nvmeMgmtMsg},
        {"spdm", MessageType::spdm},    {"sec", MessageType::securedMsg},
        {"vdpci", MessageType::vdpci},  {"vdiana", MessageType::vdiana}};

    app.add_option("-m,--msgtype", msgType, "MCTP Message type")
        ->transform(CLI::CheckedTransformer(msgTypeArgs, CLI::ignore_case))
        ->required();
    app.add_option("-b,--binding", bindingType, "MCTP binding type")
        ->transform(CLI::CheckedTransformer(bindingArgs, CLI::ignore_case))
        ->required();
    app.add_option("--vid", vendorId, "Vendor Id");
    app.add_option("--vdmtype", vdmType, "Vendor defined message type");
    app.add_option("--vdmmask", vdmMask, "Vendor defined message type mask");

    CLI11_PARSE(app, argc, argv);

    boost::asio::io_context io;
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    mctpw::MCTPConfiguration config;

    if (msgType == mctpw::MessageType::vdpci && vendorId)
    {
        if (vdmType)
        {
            config = mctpw::MCTPConfiguration(msgType, bindingType, vendorId,
                                              vdmType, vdmMask);
        }
        else
        {
            config = mctpw::MCTPConfiguration(msgType, bindingType, vendorId);
        }
    }
    else
    {
        config = mctpw::MCTPConfiguration(msgType, bindingType);
    }
    bool ctrlC = false;
    signals.async_wait(
        [&io, &ctrlC](const boost::system::error_code&, const int&) {
            std::cerr << "Ctrl C" << '\n';
            ctrlC = true;
            std::cerr << "Stopping IO" << '\n';
            io.stop();
        });

    boost::asio::spawn(
        io, [&io, &config, &ctrlC](boost::asio::yield_context yield) {
            mctpw::MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);
            mctpWrapper.detectMctpEndpoints(yield);

            mctpWrapper.getOwnEIDs([](mctpw::OwnEIDChange eidChange) {
                mctpw::OwnEIDChange::EIDChangeData* eidChangeData =
                    reinterpret_cast<mctpw::OwnEIDChange::EIDChangeData*>(
                        eidChange.context);
                std::cerr << "EID " << static_cast<int>(eidChangeData->eid)
                          << " on " << eidChangeData->service;
            });

            boost::asio::deadline_timer timer(io);
            timer.expires_from_now(boost::posix_time::seconds(10));
            boost::system::error_code ec;
            timer.async_wait(yield[ec]);
        });

    io.run();
    return 0;
}
