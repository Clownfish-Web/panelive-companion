#pragma once
#include <string>
#include <string_view>
namespace studio_remote
{
std::string obs_websocket_authentication(std::string_view password,
                                         std::string_view salt,
                                         std::string_view challenge);
}
