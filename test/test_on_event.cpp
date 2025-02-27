#include "mctp_wrapper.hpp"
#include "stubs/stub_process.hpp"

#include <gtest/gtest.h>

std::shared_ptr<Process> mctpd;

TEST(MCTPDynamicTest, Test1)
{
    using namespace mctpw;
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);
    auto callbackSuccess = [](mctpw::OwnEIDChange&) {};

    MCTPWrapper mctpWrapper(
        io, config, [](void*, const Event& evt, boost::asio::yield_context&) {
            std::cout << "EID. " << static_cast<int>(evt.eid) << '\n';
        });

    boost::asio::spawn(
        io,
        [this, &mctpWrapper, &io,
         &callbackSuccess](boost::asio::yield_context yield) {
            mctpWrapper.detectMctpEndpoints(yield);

            auto writeToProcess = [this, &io](std::array<uint8_t, 4> data,
                                              boost::asio::yield_context yield,
                                              uint16_t timeout = 200) {
                mctpd->writeData(data);

                boost::asio::steady_timer timer(
                    io, std::chrono::milliseconds(timeout));
                timer.async_wait(yield);
            };

            mctpWrapper.getOwnEIDs(callbackSuccess);
            uint8_t validEID1 = 3;
            uint8_t validEID2 = 4;

            std::array<uint8_t, 4> data = {
                static_cast<uint8_t>(OnMCTPEvtEnum::addNewInterface), 0, 0, 0};
            writeToProcess(data, yield, 1000);

            data[0] = static_cast<uint8_t>(OnMCTPEvtEnum::propertiesChange);
            data[1] = validEID1;
            writeToProcess(data, yield);

            data[0] = static_cast<uint8_t>(OnMCTPEvtEnum::removeEID);
            data[1] = validEID2;
            writeToProcess(data, yield);

            data[0] = static_cast<uint8_t>(OnMCTPEvtEnum::interfaceRemove);
            writeToProcess(data, yield);

            data[0] = static_cast<uint8_t>(OnMCTPEvtEnum::messageRecieve);

            writeToProcess(data, yield);
            io.stop();
        },
        {});

    io.run_for(std::chrono::seconds(mesonTestTimeout));
}

int main(int argc, char** argv)
{
    mctpd = std::make_shared<Process>(std::make_shared<MCTPDynamic>());
    if (mctpd->isChildProcess())
    {
        std::cout << "Running MCTPD" << '\n';
        mctpd->run();
        std::cout << "Exiting MCTPD" << '\n';
        return 0;
    }
    sleep(1);
    ::testing::InitGoogleTest(&argc, argv);
    auto status = RUN_ALL_TESTS();
    std::array<uint8_t, 4> data = {static_cast<uint8_t>(OnMCTPEvtEnum::stopIO),
                                   0, 0, 0};
    mctpd->writeData(data);
    mctpd->wait();
    return status;
}
