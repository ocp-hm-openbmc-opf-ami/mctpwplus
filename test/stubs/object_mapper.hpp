#pragma once
#include "dbus_server.hpp"

#include <boost/asio.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <unordered_map>

using ServicesT = std::unordered_map<std::string, std::vector<std::string>>;

class ObjectMapper : public SDBusServer
{
  public:
    ObjectMapper() : SDBusServer("xyz.openbmc_project.ObjectMapper")
    {
    }
    virtual void init() override
    {
        SDBusServer::init();
        objMapInterface = objectServer->add_unique_interface(
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper");
        objMapInterface->register_method(
            "GetObject",
            [this](std::string, std::vector<std::string> interfaces) {
                std::cout << "ObjectMapper method call" << '\n';
                if (getObjectHandler)
                {
                    return getObjectHandler();
                }
                ServicesT services;
                std::string serviceName = "xyz.openbmc_project.mctp-emulator";
                services.emplace(serviceName, interfaces);
                return services;
            });
        objMapInterface->initialize();
    }

    void setGetObjectHandler(std::function<ServicesT()> handler)
    {
        getObjectHandler = std::move(handler);
    }

  protected:
    std::unique_ptr<sdbusplus::asio::dbus_interface> objMapInterface;
    std::function<ServicesT()> getObjectHandler;
};

inline std::shared_ptr<Task> getObjectMapperProcess()
{
    return std::make_shared<ObjectMapper>();
}
