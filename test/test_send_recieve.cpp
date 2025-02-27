#include "mctp_wrapper.hpp"
#include "stubs/stub_process.hpp"

#include <gtest/gtest.h>

TEST(SendReciveCall, SMBus)
{
    using namespace mctpw;
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);

    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);

    size_t callbackCount = 0;
    auto recvCBSuccess =
        [&callbackCount](boost::system::error_code ec,
                         const std::vector<uint8_t>& response) {
            const std::vector<uint8_t> actual = {02};

            EXPECT_FALSE(ec);
            callbackCount++;
            EXPECT_EQ(response, actual);
        };
    auto recvCBFailure =
        [&callbackCount](boost::system::error_code ec,
                         const std::vector<uint8_t>& response) {
            EXPECT_TRUE(ec);
            callbackCount++;
            EXPECT_EQ(response.size(), 0);
        };

    boost::asio::spawn(
        io,
        [&](boost::asio::yield_context yield) {
            mctpWrapper.detectMctpEndpoints(yield);

            uint8_t eidValid = 10;
            std::vector<uint8_t> request1 = {01, 82, 02, 51};
            mctpWrapper.sendReceiveAsync(recvCBSuccess, eidValid, request1,
                                         std::chrono::milliseconds(100));

            uint8_t eidInvalid = 11;
            std::vector<uint8_t> request2 = {02, 82, 02, 51};
            mctpWrapper.sendReceiveAsync(recvCBFailure, eidInvalid, request2,
                                         std::chrono::milliseconds(100));

            std::vector<uint8_t> request3 = {00, 82, 02, 51};
            mctpWrapper.sendReceiveAsync(recvCBFailure, eidValid, request3,
                                         std::chrono::milliseconds(100));

            auto ec = mctpWrapper.sendReceiveYield(
                yield, eidValid, request1, std::chrono::milliseconds(200));
            EXPECT_FALSE(ec.first);
            EXPECT_EQ(ec.second.front(), 2);

            ec = mctpWrapper.sendReceiveYield(yield, eidInvalid, request2,
                                              std::chrono::milliseconds(200));
            EXPECT_TRUE(ec.first);

            ec = mctpWrapper.sendReceiveYield(yield, eidInvalid, request3,
                                              std::chrono::milliseconds(200));
            EXPECT_TRUE(ec.first);

            ec = mctpWrapper.sendReceiveBlocked(eidValid, request1,
                                                std::chrono::milliseconds(100));
            EXPECT_FALSE(ec.first);
            EXPECT_EQ(ec.second.front(), 2);

            ec = mctpWrapper.sendReceiveBlocked(eidInvalid, request2,
                                                std::chrono::milliseconds(100));
            EXPECT_TRUE(ec.first);

            ec = mctpWrapper.sendReceiveBlocked(eidValid, request3,
                                                std::chrono::milliseconds(100));
            EXPECT_TRUE(ec.first);
            size_t retry = 5;
            while (retry-- > 0)
            {
                boost::asio::steady_timer timer(io);
                timer.expires_after(std::chrono::milliseconds(200));
                if (callbackCount != 3)
                {
                    std::cout << "Waiting for send callback" << '\n';
                    timer.async_wait(yield);
                }
                else
                {
                    break;
                }
            }
            io.stop();
        },
        {});

    io.run_for(std::chrono::seconds(15));
    EXPECT_EQ(callbackCount, 3);
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
