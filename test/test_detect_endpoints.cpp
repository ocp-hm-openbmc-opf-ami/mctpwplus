#include "../mctp_wrapper.hpp"
#include <gtest/gtest.h>
#include "stubs/stub_process.hpp"

TEST(DetectEndPointsTest, Async)
{
    using namespace mctpw;
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);
    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);
    mctpWrapper.detectMctpEndpointsAsync(
        [&mctpWrapper, &io](boost::system::error_code, void*) {
            auto eidMap = mctpWrapper.getEndpointMap();
            for (auto& entry : eidMap)
            {
                std::cout << " Eid = "<<static_cast<int>(entry.first) << '\n';
            }
            EXPECT_EQ(eidMap.size(), 2);
            EXPECT_TRUE(eidMap.contains(9));
            EXPECT_TRUE(eidMap.contains(10));

            io.stop();
        });
    io.run();
    std::cout << "Wrapper exited" << '\n';
}

TEST(DetectEndPointsTest, Yield)
{
    using namespace mctpw;
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);
    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);

    boost::asio::spawn(
        io, [&mctpWrapper, &io](boost::asio::yield_context yield) {
            mctpWrapper.detectMctpEndpoints(yield);
            auto eidMap = mctpWrapper.getEndpointMap();
            for (const auto& [eid, serviceName] : eidMap)
            {
                std::cout << "Eid " << static_cast<int>(eid) << " on "
                          << serviceName.second << '\n';
            }
            EXPECT_EQ(eidMap.size(), 2);
            EXPECT_TRUE(eidMap.contains(9));
            EXPECT_TRUE(eidMap.contains(10));
            io.stop();
        });
    io.run();
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
