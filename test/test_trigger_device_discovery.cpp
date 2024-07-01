#include "mctp_wrapper.hpp"
#include "stubs/stub_process.hpp"

#include <gtest/gtest.h>

TEST(TriggerDeviceDiscovery, PcieVdm)
{
    using namespace mctpw;
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverPcieVdm);

    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);

    boost::asio::spawn(io, [&](boost::asio::yield_context yield) {
        mctpWrapper.detectMctpEndpoints(yield);

        uint8_t eidValid = 10;
        EXPECT_NO_THROW(mctpWrapper.triggerMCTPDeviceDiscovery(eidValid));

        uint8_t eidInvalid = 11;
        EXPECT_NO_THROW(mctpWrapper.triggerMCTPDeviceDiscovery(eidInvalid));

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
