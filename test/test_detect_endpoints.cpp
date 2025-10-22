#include "mctp_wrapper.hpp"
#include "stubs/stub_process.hpp"
#include "utils.hpp"

#include <gtest/gtest.h>

TEST(DetectEndPointsTest, Async)
{
    using namespace mctpw;
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);
    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);
    mctpWrapper.detectMctpEndpointsAsync(
        [&mctpWrapper, &io](boost::system::error_code, void*) {
            auto eidMap = mctpWrapper.getEndpointMapExtended();
            for (auto& entry : eidMap)
            {
                std::cout << entry << '\n';
            }
            EXPECT_EQ(eidMap.size(), 2);
            EXPECT_TRUE(eidMap.contains(DeviceID(9, 0)));
            EXPECT_TRUE(eidMap.contains(DeviceID(10, 0)));
            io.stop();
        });
    io.run_for(std::chrono::seconds(mesonTestTimeout));
    std::cout << "Wrapper exited" << '\n';
}

TEST(DetectEndPointsTest, Yield)
{
    using namespace mctpw;
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);
    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);

    mctpw::spawn(io, [&mctpWrapper, &io](boost::asio::yield_context yield) {
        mctpWrapper.detectMctpEndpoints(yield);
        auto eidMap = mctpWrapper.getEndpointMapExtended();
        EXPECT_EQ(eidMap.size(), 2);
        EXPECT_TRUE(eidMap.contains(DeviceID(9, 0)));
        EXPECT_TRUE(eidMap.contains(DeviceID(10, 0)));
        io.stop();
    });
    io.run_for(std::chrono::seconds(mesonTestTimeout));
}

TEST(DetectEndPointsTest, Sync)
{
    using namespace mctpw;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);
    boost::asio::io_context io;
    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);

    mctpWrapper.detectMctpEndpoints();
    auto eidMap = mctpWrapper.getEndpointMapExtended();
    EXPECT_EQ(eidMap.size(), 2);
    EXPECT_TRUE(eidMap.contains(DeviceID(9, 0)));
    EXPECT_TRUE(eidMap.contains(DeviceID(10, 0)));
    io.stop();
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
