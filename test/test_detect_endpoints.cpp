#include "../mctp_wrapper.hpp"
#include "stubs/mctp_constants.hpp"
#include "stubs/mctpd.hpp"
#include "stubs/object_mapper.hpp"

#include <gtest/gtest.h>

class StubProcesses
{
  public:
    StubProcesses()
    {
        std::cout << "Setting up test" << '\n';
        auto mctpSMBus = service_names::mctpSMBus().data();

        auto mctpdTask = std::make_shared<MCTPSMBus>();
        mctpd = std::make_shared<Process>(mctpdTask);
        if (mctpd->isChildProcess())
        {
            mctpdTask->addEID(9, 0b01110011);
            std::vector<uint16_t> vdmTypes = {256, 2, 3, 5};
            mctpdTask->addEID(10, 0b01110011, vdmTypes);
            std::cout << "Running MCTPD" << '\n';
            mctpd->run();
            std::cout << "Exiting MCTPD" << '\n';
            return;
        }

        auto objMapperTask = std::make_shared<ObjectMapper>();
        objectMapper = std::make_shared<Process>(objMapperTask);
        if (objectMapper->isChildProcess())
        {
            objMapperTask->setGetObjectHandler([mctpSMBus]() {
                ServicesT services;
                std::string serviceName = mctpSMBus;
                std::vector<std::string> interfaces{"a", "b", "c"};
                services.emplace(serviceName, interfaces);
                return services;
            });
            std::cout << "Running ObjectMapper" << '\n';
            objectMapper->run();
            std::cout << "Exiting ObjectMapper" << '\n';
            return;
        }

        // Small delay for dbus setup in child processes
        usleep(200 * 1000);
    }

    bool isParentProcess()
    {
        return !mctpd->isChildProcess() && !objectMapper->isChildProcess();
    }

    void wait()
    {
        std::array<uint8_t, 1> data = {'A'};
        mctpd->writeData(data);
        objectMapper->writeData(data);
        mctpd->wait();
        objectMapper->wait();
    }
    ~StubProcesses()
    {
        if (isParentProcess())
        {
            wait();
        }
    }

  protected:
    std::shared_ptr<Process> mctpd;
    std::shared_ptr<Process> objectMapper;
};

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
