#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace service_names
{
constexpr std::string_view mctpSMBus()
{
    return "test.mctp.SMBus";
}
constexpr std::string_view mctpI3C()
{
    return "test.mctp.I3C";
}
} // namespace service_names

namespace interfaces
{
constexpr std::string_view mctpBase()
{
    return "xyz.openbmc_project.MCTP.Base";
}
constexpr std::string_view mctpSMBus()
{
    return "xyz.openbmc_project.MCTP.Binding.SMBus";
}
inline std::vector<std::string> getMCTPBaseVector()
{
    std::vector<std::string> intf;
    intf.push_back(std::string(mctpBase().data()));
    return intf;
}
} // namespace interfaces