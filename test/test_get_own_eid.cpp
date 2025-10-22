#include "mctp_wrapper.hpp"
#include "stubs/stub_process.hpp"
#include "utils.hpp"

#include <gtest/gtest.h>

TEST(GetOwnEIDs, SMBus)
{
    using namespace mctpw;
    boost::asio::io_context io;
    auto conn = std::make_shared<sdbusplus::asio::connection>(io);
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);

    MCTPWrapper mctpWrapper(conn, config, nullptr, nullptr);

    size_t callbackCount = 0;
    auto callbackSuccess = [&callbackCount](mctpw::DeviceID devID) {
        callbackCount++;
        uint8_t expectedEID = 4;
        auto actulEID = devID.mctpEID();
        EXPECT_EQ(actulEID, expectedEID);
    };
    mctpw::spawn(conn->get_io_context(), [&](boost::asio::yield_context yield) {
        mctpWrapper.detectMctpEndpoints(yield);
        mctpWrapper.getOwnEIDs(callbackSuccess);

        size_t callbackCount = 0;
        size_t retry = 5;
        while (retry-- > 0)
        {
            boost::asio::steady_timer timer(io);
            timer.expires_after(std::chrono::milliseconds(200));
            if (callbackCount != 1)
            {
                timer.async_wait(yield);
            }
            else
            {
                break;
            }
        }
        io.stop();
    });

    io.run_for(std::chrono::seconds(mesonTestTimeout));
    EXPECT_EQ(callbackCount, 1);
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
