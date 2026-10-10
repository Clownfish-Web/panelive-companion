#include "windows_lan_address.hpp"

// Windows networking headers must precede Windows.h.
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
// clang-format on

#include <charconv>
#include <cstddef>
#include <cstring>
#include <memory>
#include <studio_remote/discovery/lan_address_selection.hpp>
#include <vector>

namespace studio_remote::windows
{
namespace
{
std::optional<SOCKADDR_INET> parse_host_address(std::string_view host)
{
  if (host.empty() || host.find('\0') != std::string_view::npos)
    return std::nullopt;
  const bool bracketed = host.front() == '[';
  if (bracketed)
  {
    if (host.back() != ']')
      return std::nullopt;
    host = host.substr(1, host.size() - 2);
  }

  SOCKADDR_INET address{};
  if (!bracketed && InetPtonA(AF_INET, std::string(host).c_str(), &address.Ipv4.sin_addr) == 1)
  {
    address.si_family = AF_INET;
    return address;
  }

  if (const auto zone = host.find('%'); zone != std::string_view::npos)
  {
    const auto scope = host.substr(zone + 1);
    const auto parsed = std::from_chars(scope.data(), scope.data() + scope.size(),
                                        address.Ipv6.sin6_scope_id);
    if (scope.empty() || parsed.ec != std::errc{} || parsed.ptr != scope.data() + scope.size())
      return std::nullopt;
    host = host.substr(0, zone);
  }
  if (InetPtonA(AF_INET6, std::string(host).c_str(), &address.Ipv6.sin6_addr) != 1)
    return std::nullopt;
  address.si_family = AF_INET6;
  if (IN6_IS_ADDR_V4MAPPED(&address.Ipv6.sin6_addr))
  {
    SOCKADDR_INET ipv4{};
    ipv4.si_family = AF_INET;
    std::memcpy(&ipv4.Ipv4.sin_addr, &address.Ipv6.sin6_addr.u.Byte[12], sizeof(IN_ADDR));
    return ipv4;
  }
  return address;
}

bool same_host_address(const SOCKADDR_INET& host, const SOCKADDR_INET& local)
{
  if (host.si_family != local.si_family)
    return false;
  if (host.si_family == AF_INET)
    return host.Ipv4.sin_addr.s_addr == local.Ipv4.sin_addr.s_addr;
  return IN6_ADDR_EQUAL(&host.Ipv6.sin6_addr, &local.Ipv6.sin6_addr) &&
         (host.Ipv6.sin6_scope_id == 0 || host.Ipv6.sin6_scope_id == local.Ipv6.sin6_scope_id);
}

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

bool is_local_host_address(std::string_view host)
{
  if (host.size() == 9 && _strnicmp(host.data(), "localhost", host.size()) == 0)
    return true;
  const auto address = parse_host_address(host);
  if (!address)
    return false;
  if ((address->si_family == AF_INET && address->Ipv4.sin_addr.S_un.S_un_b.s_b1 == 127) ||
      (address->si_family == AF_INET6 && IN6_IS_ADDR_LOOPBACK(&address->Ipv6.sin6_addr)))
    return true;

  MIB_UNICASTIPADDRESS_TABLE* raw_table = nullptr;
  if (GetUnicastIpAddressTable(AF_UNSPEC, &raw_table) != NO_ERROR)
    return false;
  const std::unique_ptr<MIB_UNICASTIPADDRESS_TABLE, decltype(&FreeMibTable)> table(raw_table,
                                                                                &FreeMibTable);
  for (ULONG index = 0; index < table->NumEntries; ++index)
  {
    const auto& entry = table->Table[index];
    if ((entry.DadState != IpDadStatePreferred && entry.DadState != IpDadStateDeprecated) ||
        !same_host_address(*address, entry.Address))
      continue;
    MIB_IF_ROW2 adapter{};
    adapter.InterfaceLuid = entry.InterfaceLuid;
    if (GetIfEntry2(&adapter) == NO_ERROR && adapter.OperStatus == IfOperStatusUp)
      return true;
  }
  return false;
}
} // namespace studio_remote::windows
