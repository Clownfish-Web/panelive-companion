#include "windows_lan_address.hpp"

// Windows networking headers must precede Windows.h.
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <iphlpapi.h>
// clang-format on

#include <cstddef>
#include <memory>
#include <studio_remote/discovery/lan_address_selection.hpp>
#include <vector>

namespace studio_remote::windows
{
namespace
{
bool is_private_ipv4(const IN_ADDR& address)
{
  const auto* bytes = reinterpret_cast<const unsigned char*>(&address);
  return is_private_lan_ipv4({bytes[0], bytes[1], bytes[2], bytes[3]});
}

bool is_physical_lan_adapter(const IP_ADAPTER_ADDRESSES& adapter)
{
  return adapter.IfType == IF_TYPE_ETHERNET_CSMACD || adapter.IfType == IF_TYPE_IEEE80211;
}

std::vector<LanAddressCandidate> read_lan_candidates()
{
  constexpr ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST |
                          GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  ULONG size = 0;
  if (GetAdaptersAddresses(AF_INET, flags, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW ||
      size == 0)
    return {};

  auto storage = std::make_unique<std::byte[]>(size);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.get());
  if (GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &size) != NO_ERROR)
    return {};

  std::vector<LanAddressCandidate> candidates;
  for (auto* adapter = adapters; adapter; adapter = adapter->Next)
  {
    if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
        (adapter->Flags & IP_ADAPTER_RECEIVE_ONLY) != 0)
      continue;

    for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next)
    {
      if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET ||
          unicast->DadState != IpDadStatePreferred)
        continue;

      const auto& address =
          reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr)->sin_addr;
      if (!is_private_ipv4(address))
        continue;

      char text[INET_ADDRSTRLEN]{};
      if (!InetNtopA(AF_INET, &address, text, sizeof(text)))
        continue;

      candidates.push_back({text,
                            is_physical_lan_adapter(*adapter),
                            adapter->FirstGatewayAddress != nullptr,
                            adapter->Ipv4Metric});
    }
  }
  return candidates;
}
} // namespace

std::vector<std::string> available_lan_ipv4_addresses()
{
  return ordered_lan_ipv4_addresses(read_lan_candidates());
}

std::optional<std::string> reachable_lan_ipv4_address()
{
  const auto addresses = available_lan_ipv4_addresses();
  return addresses.empty() ? std::nullopt : std::optional(addresses.front());
}
} // namespace studio_remote::windows
