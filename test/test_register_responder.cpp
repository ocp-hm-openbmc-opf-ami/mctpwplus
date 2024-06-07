#include "mctp_wrapper.hpp"
#include "stubs/stub_process.hpp"

#include <gtest/gtest.h>

TEST(RegisterResponder, SMBus)
{
    using namespace mctpw;
    boost::asio::io_context io;
    auto conn = std::make_shared<sdbusplus::asio::connection>(io);

    MCTPConfiguration pldmConfig(mctpw::MessageType::pldm,
                                 mctpw::BindingType::mctpOverSmBus);
    MCTPWrapper mctpWrapperForPldm(conn, pldmConfig, nullptr, nullptr);

    MCTPConfiguration vdpciConfig(mctpw::MessageType::vdpci,
                                  mctpw::BindingType::mctpOverSmBus, 0x8086, 10,
                                  0xFFF);
    MCTPWrapper mctpWrapperForVdpci(conn, vdpciConfig, nullptr, nullptr);
    VersionFields specVersion = {0xF1, 0xF1, 0xF0, 0};
    VersionFields exceptionCase = {0x00, 0xF1, 0xF0, 0};

    boost::asio::spawn(io, [&](boost::asio::yield_context yield) {
        mctpWrapperForPldm.detectMctpEndpoints(yield);
        mctpWrapperForVdpci.detectMctpEndpoints(yield);

        std::vector<VersionFields> emptyVersion = {};
        auto rcvStatus = mctpWrapperForVdpci.registerResponder(emptyVersion);
        EXPECT_EQ(rcvStatus, boost::system::errc::io_error);

        rcvStatus = mctpWrapperForVdpci.registerResponder(specVersion);
        EXPECT_EQ(rcvStatus, boost::system::errc::success);

        rcvStatus = mctpWrapperForPldm.registerResponder(specVersion);
        EXPECT_EQ(rcvStatus, boost::system::errc::success);

        rcvStatus = mctpWrapperForVdpci.registerResponder(exceptionCase);
        EXPECT_EQ(rcvStatus, boost::system::errc::io_error);

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
