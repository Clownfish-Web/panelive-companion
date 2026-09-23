#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace studio_remote::macos
{
/// Blocking native snapshot. Call on the configuration worker, not in a request/UI callback.
std::vector<std::string> available_lan_ipv4_addresses();
std::string listener_specification(std::uint16_t port, bool allow_non_private_interfaces);
} // namespace studio_remote::macos
