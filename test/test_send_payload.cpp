#include "mctp_wrapper.hpp"
#include "stubs/stub_process.hpp"
#include "utils.hpp"

#include <gtest/gtest.h>

TEST(SendCall, SMBus)
{
    using namespace mctpw;
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);
    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);

    size_t callbackCount = 0;
    auto sendCBSucess = [&callbackCount](boost::system::error_code err,
                                         const int response) {
        const int actual = 0;
        callbackCount++;
        EXPECT_EQ(err.value(), boost::system::errc::success);
        EXPECT_EQ(response, actual);
    };

    auto sendCBFailure = [&callbackCount](boost::system::error_code err,
                                          const int response) {
        const int actual = -1;
        callbackCount++;
        EXPECT_EQ(err, boost::system::errc::io_error);
        EXPECT_EQ(err.value(), boost::system::errc::io_error);
        EXPECT_EQ(response, actual);
    };
    auto sendCBFailureException =
        [&callbackCount](boost::system::error_code err, const int response) {
            const int actual = 0;
            callbackCount++;
            EXPECT_EQ(err.value(), boost::system::errc::invalid_argument);
            EXPECT_EQ(response, actual);
        };

    mctpw::spawn(io, [&](boost::asio::yield_context yield) {
        mctpWrapper.detectMctpEndpoints(yield);

        uint8_t eidValid = 10;
        uint8_t eidNotExists = 11;
        std::vector<uint8_t> request1 = {01, 82, 02, 51};
        std::vector<uint8_t> request2 = {02, 82, 02, 51};
        std::vector<uint8_t> request3 = {00, 82, 02, 51};

        mctpWrapper.sendAsync(sendCBSucess, eidValid, 0, false, request1);
        mctpWrapper.sendAsync(sendCBFailure, eidNotExists, 0, false, request2);
        mctpWrapper.sendAsync(sendCBFailureException, eidValid, 0, false,
                              request3);

        auto status =
            mctpWrapper.sendYield(yield, eidValid, 0, false, request1);
        EXPECT_FALSE(status.first);
        EXPECT_EQ(status.first.value(), boost::system::errc::success);
        EXPECT_EQ(status.second, 0);

        status = mctpWrapper.sendYield(yield, eidValid, 0, false, request2);
        EXPECT_EQ(status.first.value(), boost::system::errc::success);
        EXPECT_EQ(status.second, 0);

        status = mctpWrapper.sendYield(yield, eidNotExists, 0, false, request3);
        EXPECT_EQ(status.first.value(), boost::system::errc::io_error);
        EXPECT_EQ(status.second, -1);
        size_t retry = 5;
        while (retry-- > 0)
        {
            boost::asio::steady_timer timer(io);
            timer.expires_after(std::chrono::milliseconds(200));
            if (callbackCount != 3)
            {
                timer.async_wait(yield);
            }
            else
            {
                break;
            }
        }
        status =
            mctpWrapper.sendBlocked(DeviceID(eidValid, 0), 0, false, request1);
        EXPECT_FALSE(status.first);
        EXPECT_EQ(status.first.value(), boost::system::errc::success);
        EXPECT_EQ(status.second, 0);

        status =
            mctpWrapper.sendBlocked(DeviceID(eidValid, 0), 0, false, request2);
        EXPECT_EQ(status.first.value(), boost::system::errc::success);
        EXPECT_EQ(status.second, 0);

        status = mctpWrapper.sendBlocked(DeviceID(eidNotExists, 0), 0, false,
                                         request3);
        EXPECT_EQ(status.first.value(), boost::system::errc::io_error);
        EXPECT_EQ(status.second, -1);
        io.stop();
    });
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
