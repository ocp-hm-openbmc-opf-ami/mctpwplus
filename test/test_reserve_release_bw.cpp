#include "mctp_wrapper.hpp"
#include "stubs/stub_process.hpp"
#include "utils.hpp"

#include <gtest/gtest.h>

TEST(ReserveBandwidth, mctpOverSmBus)
{
    using namespace mctpw;
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);

    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);

    mctpw::spawn(io, [&](boost::asio::yield_context yield) {
        mctpWrapper.detectMctpEndpoints(yield);

        uint8_t eidValid = 10;
        auto status = mctpWrapper.reserveBandwidth(yield, eidValid, 100);
        EXPECT_EQ(status, 0);

        auto statusFail = mctpWrapper.reserveBandwidth(yield, eidValid, 100);
        EXPECT_EQ(statusFail, -1);

        statusFail = mctpWrapper.reserveBandwidth(yield, eidValid, 100);
        EXPECT_EQ(statusFail, -1);

        uint8_t eidInvalid = 15;
        statusFail = mctpWrapper.reserveBandwidth(yield, eidInvalid, 100);
        EXPECT_EQ(statusFail, -1);

        io.stop();
    });

    io.run_for(std::chrono::seconds(15));
}

TEST(RealeaseBandwidth, mctpOverSmBus)
{
    using namespace mctpw;
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);

    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);

    mctpw::spawn(io, [&](boost::asio::yield_context yield) {
        mctpWrapper.detectMctpEndpoints(yield);

        uint8_t eidValid = 10;
        auto status = mctpWrapper.releaseBandwidth(yield, eidValid);
        EXPECT_EQ(status, 0);

        auto statusFail = mctpWrapper.releaseBandwidth(yield, eidValid);
        EXPECT_EQ(statusFail, -1);

        statusFail = mctpWrapper.releaseBandwidth(yield, eidValid);
        EXPECT_EQ(statusFail, -1);

        uint8_t eidInvalid = 15;
        statusFail = mctpWrapper.releaseBandwidth(yield, eidInvalid);
        EXPECT_EQ(statusFail, -1);

        io.stop();
    });

    io.run_for(std::chrono::seconds(15));
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
