#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote::windows
{
/// Returns active private IPv4 addresses ordered with the preferred LAN address first.
std::vector<std::string> available_lan_ipv4_addresses();

/// Reads active Windows adapters and returns the preferred private IPv4 LAN address.
std::optional<std::string> reachable_lan_ipv4_address();

/// Recognizes localhost, numeric loopback and unicast addresses assigned to this PC.
/// Reads current interfaces on each call; does not resolve arbitrary DNS names.
bool is_local_host_address(std::string_view host);
} // namespace studio_remote::windows
