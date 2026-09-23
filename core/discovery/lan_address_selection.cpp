#include <studio_remote/discovery/lan_address_selection.hpp>

#include <algorithm>
#include <tuple>

namespace studio_remote
{
namespace
{
bool candidate_precedes(const LanAddressCandidate& left, const LanAddressCandidate& right)
{
  return std::tuple{
             !left.physical_adapter, !left.has_gateway, left.interface_metric, left.address} <
         std::tuple{
             !right.physical_adapter, !right.has_gateway, right.interface_metric, right.address};
}

} // namespace

std::optional<std::string>
select_reachable_lan_ipv4(std::span<const LanAddressCandidate> candidates)
{
  if (candidates.empty())
    return std::nullopt;

  const auto best = std::min_element(candidates.begin(), candidates.end(), candidate_precedes);
  return best->address;
}

std::optional<std::string> select_configured_lan_ipv4(std::span<const std::string> addresses,
                                                      std::string_view configured)
{
  const auto saved = std::ranges::find(addresses, configured);
  if (saved != addresses.end())
    return *saved;
  return addresses.empty() ? std::nullopt : std::optional(addresses.front());
}

std::vector<std::string> ordered_lan_ipv4_addresses(std::vector<LanAddressCandidate> candidates)
{
  std::ranges::sort(candidates, candidate_precedes);
  std::vector<std::string> addresses;
  for (const auto& candidate : candidates)
  {
    if (std::ranges::find(addresses, candidate.address) == addresses.end())
      addresses.push_back(candidate.address);
  }
  return addresses;
}

bool is_private_lan_ipv4(const std::array<std::uint8_t, 4>& bytes) noexcept
{
  return bytes[0] == 10 || (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) ||
         (bytes[0] == 192 && bytes[1] == 168);
}
bool is_private_listener_ipv4(const std::array<std::uint8_t, 4>& bytes) noexcept
{
  return is_private_lan_ipv4(bytes) || (bytes[0] == 169 && bytes[1] == 254);
}

bool is_private_listener_ipv6(std::span<const std::uint8_t, 16> bytes) noexcept
{
  return (bytes[0] & 0xfe) == 0xfc;
}
} // namespace studio_remote
