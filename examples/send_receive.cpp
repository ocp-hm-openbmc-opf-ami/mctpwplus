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

#include <boost/asio.hpp>
#include <iostream>

int main(int argc, char* argv[])
{
    constexpr uint8_t defaultEId = 8;

    uint8_t eid =
        argc < 2 ? defaultEId : static_cast<uint8_t>(std::stoi(argv[1]));
    uint8_t networkId =
        argc < 3 ? 1 : static_cast<uint8_t>(std::stoi(argv[2]));
    using namespace mctpw;
    boost::asio::io_context io;
    mctpw::DeviceID deviceId(eid, networkId);

    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait(
        [&io](const boost::system::error_code&, const int&) { io.stop(); });

    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);
    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);

    auto recvCB = [](boost::system::error_code err,
                     const std::vector<uint8_t>& response) {
        if (err)
        {
            std::cout << "Async Error " << err.message() << '\n';
        }
        else
        {
            std::cout << "Async Response ";
            for (int n : response)
            {
                std::cout << n << ' ';
            }
            std::cout << '\n';
        }
    };

    auto registerCB = [deviceId, recvCB, &mctpWrapper,
                       &io](boost::system::error_code ec, void*) {
        if (ec)
        {
            std::cout << "Error: " << ec.message() << std::endl;
            return;
        }
        auto& ep = mctpWrapper.getEndpointMapExtended();
        for (auto& i : ep)
        {
            std::cout << "EID:" << static_cast<unsigned>(i.first.id)
                      << " Bus:" << i.second.first
                      << " Service:" << i.second.second << std::endl;
        }
        // GetVersion request for PLDM Base
        std::vector<uint8_t> request = {1, 143, 0, 3, 0, 0, 0, 0, 1, 0};
        mctpWrapper.sendReceiveAsync(recvCB, deviceId, request,
                                     std::chrono::milliseconds(100));
        boost::asio::spawn(io, [&mctpWrapper,
                                deviceId](boost::asio::yield_context yield) {
            // GetUID request
            std::vector<uint8_t> request2 = {1, 143, 0, 3, 0, 0, 0, 0, 1, 0};
            std::cout << "Before sendReceiveYield" << std::endl;
            auto rcvStatus = mctpWrapper.sendReceiveYield(
                yield, deviceId, request2, std::chrono::milliseconds(100));
            if (rcvStatus.first)
            {
                std::cout << "Yield Error " << rcvStatus.first.message()
                          << '\n';
            }
            else
            {
                std::cout << "Yield Response ";
                for (int n : rcvStatus.second)
                {
                    std::cout << n << ' ';
                }
                std::cout << '\n';
            }
            return;
        });
    };

    mctpWrapper.detectMctpEndpointsAsync(registerCB);

    io.run();
    return 0;
}
