/*
// Copyright (c) 2025 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#pragma once

#include <unistd.h>

#include <boost/asio/spawn.hpp>
#include <expected>
#include <sdbusplus/asio/connection.hpp>

namespace mctpw
{

class ScopedFD
{
  public:
    explicit constexpr ScopedFD(int fd) : fd(fd)
    {
    }
    ~ScopedFD()
    {
        if (fd >= 0)
        {
            close(fd);
        }
    }
    ScopedFD(const ScopedFD&) = delete;
    ScopedFD& operator=(const ScopedFD&) = delete;

  private:
    int fd = -1;
};

template <typename ReturnT, typename... Args>
ReturnT methodCall(sdbusplus::asio::connection& connection,
                   const std::string& service, const std::string& path,
                   const std::string& interface, const std::string& method,
                   Args&&... args)
{
    auto msg = connection.new_method_call(service.c_str(), path.c_str(),
                                          interface.c_str(), method.c_str());
    msg.append(std::forward<Args>(args)...);

    auto reply = connection.call(msg);

    ReturnT v;
    reply.read(v);
    return v;
}

template <typename... Args>
void methodCall(sdbusplus::asio::connection& connection,
                const std::string& service, const std::string& path,
                const std::string& interface, const std::string& method,
                Args&&... args)
{
    auto msg = connection.new_method_call(service.c_str(), path.c_str(),
                                          interface.c_str(), method.c_str());
    msg.append(std::forward<Args>(args)...);

    auto reply = connection.call(msg);
}

template <typename ReturnT, typename... Args>
std::expected<ReturnT, boost::system::error_code>
    methodCall(sdbusplus::asio::connection& connection,
               const std::string& service, const std::string& path,
               const std::string& interface, const std::string& method,
               std::optional<boost::asio::yield_context> yield, Args&&... args)
{
    if (!yield)
    {
        return methodCall<ReturnT>(connection, service, path, interface, method,
                                   std::forward<Args>(args)...);
    }

    std::variant<ReturnT> v;
    boost::system::error_code ec;
    ReturnT val = connection.yield_method_call<ReturnT>(
        *yield, ec, service, path, interface, method,
        std::forward<Args>(args)...);
    if (ec)
    {
        return std::unexpected(ec);
    }
    else
    {
        return val;
    }
}

template <typename... Args>
boost::system::error_code
    methodCall(sdbusplus::asio::connection& connection,
               const std::string& service, const std::string& path,
               const std::string& interface, const std::string& method,
               std::optional<boost::asio::yield_context> yield, Args&&... args)
{
    boost::system::error_code ec;
    if (yield)
    {
        connection.yield_method_call<void>(*yield, ec, service, path, interface,
                                           method, std::forward<Args>(args)...);
        return ec;
    }

    methodCall(connection, service, path, interface, method,
               std::forward<Args>(args)...);
    return ec;
}

template <typename Property>
Property readPropertyValue(sdbusplus::asio::connection& connection,
                           const std::string& service, const std::string& path,
                           const std::string& interface,
                           const std::string& property)
{
    using T = std::variant<Property>;
    T val = methodCall<T>(connection, service, path,
                          "org.freedesktop.DBus.Properties", "Get", interface,
                          property);
    return std::get<Property>(val);
}

template <typename Property>
std::expected<Property, boost::system::error_code>
    readPropertyValue(sdbusplus::asio::connection& connection,
                      const std::string& service, const std::string& path,
                      const std::string& interface, const std::string& property,
                      boost::asio::yield_context yield)
{
    using T = std::variant<Property>;
    auto val = methodCall<T>(
        connection, service, path, "org.freedesktop.DBus.Properties", "Get",
        std::make_optional<boost::asio::yield_context>(yield), interface,
        property);
    if (val.has_value())
    {
        return std::get<Property>(val.value());
    }
    else
    {
        return std::unexpected(val.error());
    }
}

template <typename Executor, typename F>
auto spawn(Executor&& ex, F&& function)
{
    return boost::asio::spawn(std::forward<Executor>(ex),
                              std::forward<F>(function),
                              [](const std::exception_ptr& e) {
                                  if (e)
                                  {
                                      std::rethrow_exception(e);
                                  }
                              });
}

} // namespace mctpw