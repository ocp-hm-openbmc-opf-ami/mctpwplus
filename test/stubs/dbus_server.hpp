#pragma once
#include "process_utils.hpp"

#include <boost/asio.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

class SDBusServer : public Task
{
  public:
    SDBusServer(std::string name) : dbusName(std::move(name))
    {
    }
    virtual void init()
    {
        connection = std::make_shared<sdbusplus::asio::connection>(io);
        connection->request_name(dbusName.data());
        objectServer = std::make_shared<sdbusplus::asio::object_server>(connection, true);
        objectServer->add_manager(objManagerPath);
    }
    void task() override
    {
        init();
        io.run_for(std::chrono::seconds(15));
    }
    void onData(std::span<uint8_t>) override
    {
        io.stop();
    }
    virtual void setObjManagerPath(std::string path)
    {
        objManagerPath = path;
    }

protected:
    boost::asio::io_context io;
    std::shared_ptr<sdbusplus::asio::connection> connection;
    std::shared_ptr<sdbusplus::asio::object_server> objectServer;
    std::string dbusName;
    std::string objManagerPath = "/";
};