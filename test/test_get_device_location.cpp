#include "mctp_wrapper.hpp"
#include "stubs/stub_process.hpp"

#include <gtest/gtest.h>

TEST(GetDeviceLocation, SMBus)
{
    using namespace mctpw;
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);

    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);

    boost::asio::spawn(io, [&](boost::asio::yield_context yield) {
        mctpWrapper.detectMctpEndpoints(yield);

        uint8_t eidValid = 9;
        uint8_t eidInvalid = 11;
        std::string actual = "PCIe_Slot_1";
        auto locCode = mctpWrapper.getDeviceLocation(eidValid);
        EXPECT_TRUE(locCode);
        EXPECT_EQ(*locCode, actual);

        locCode = mctpWrapper.getDeviceLocation(eidInvalid);
        EXPECT_FALSE(locCode);

        io.stop();
    }, {});
    io.run_for(std::chrono::seconds(mesonTestTimeout));
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
