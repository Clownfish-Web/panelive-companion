#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote
{
struct LanAddressCandidate
{
  std::string address;
  bool physical_adapter = false;
  bool has_gateway = false;
  std::uint32_t interface_metric = 0;
};

/// Selects the address most likely to share a reachable LAN with the mobile client.
std::optional<std::string>
select_reachable_lan_ipv4(std::span<const LanAddressCandidate> candidates);

/// Retains an available saved address or chooses the preferred first detected address.
std::optional<std::string> select_configured_lan_ipv4(std::span<const std::string> addresses,
                                                      std::string_view configured);

/// Orders and deduplicates candidates using the same preference as single-address selection.
std::vector<std::string> ordered_lan_ipv4_addresses(std::vector<LanAddressCandidate> candidates);
bool is_private_lan_ipv4(const std::array<std::uint8_t, 4>& bytes) noexcept;
bool is_private_listener_ipv4(const std::array<std::uint8_t, 4>& bytes) noexcept;
bool is_private_listener_ipv6(std::span<const std::uint8_t, 16> bytes) noexcept;
} // namespace studio_remote
