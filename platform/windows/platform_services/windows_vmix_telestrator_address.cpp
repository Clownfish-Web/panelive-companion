#include "windows_vmix_telestrator_probe.hpp"

#include <WS2tcpip.h>
#include <WinSock2.h>
#include <algorithm>
#include <cctype>

namespace studio_remote::windows
{
namespace
{
enum class AddressKind
{
  invalid,
  loopback,
  mobile
};

AddressKind classify_dns_name(std::string_view normalized)
{
  // Reject URL syntax, scoped addresses and legacy numeric IP spellings. DNS labels only.
  // Hexadecimal components in inet_addr-style numeric addresses must not pass as DNS labels.
  if (normalized.starts_with("0x") || normalized.find(".0x") != std::string::npos)
    return AddressKind::invalid;
  bool has_letter = false;
  std::size_t label_length = 0;
  char previous = '.';
  for (const unsigned char character : normalized)
  {
    if (character == '.')
    {
      if (label_length == 0 || previous == '-')
        return AddressKind::invalid;
      label_length = 0;
    }
    else
    {
      if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
            character == '-') ||
          (label_length == 0 && character == '-') || ++label_length > 63)
        return AddressKind::invalid;
      has_letter = has_letter || (character >= 'a' && character <= 'z');
    }
    previous = static_cast<char>(character);
  }
  if (!has_letter || label_length == 0 || previous == '-')
    return AddressKind::invalid;
  return AddressKind::mobile;
}

AddressKind classify_address(std::string_view host)
{
  if (host.empty() || host.size() > 253)
    return AddressKind::invalid;
  std::string normalized(host);
  std::ranges::transform(normalized,
                         normalized.begin(),
                         [](unsigned char value)
                         { return static_cast<char>(std::tolower(value)); });
  if (normalized.back() == '.')
    normalized.pop_back();
  if (normalized == "localhost" || normalized.ends_with(".localhost"))
    return AddressKind::loopback;
  IN_ADDR ipv4{};
  if (InetPtonA(AF_INET, normalized.c_str(), &ipv4) == 1)
  {
    const auto first = ipv4.S_un.S_un_b.s_b1;
    if (first == 127)
      return AddressKind::loopback;
    if (first == 0 || first >= 224 || (first == 169 && ipv4.S_un.S_un_b.s_b2 == 254))
      return AddressKind::invalid;
    return AddressKind::mobile;
  }
  IN6_ADDR ipv6{};
  if (InetPtonA(AF_INET6, normalized.c_str(), &ipv6) == 1)
  {
    if (IN6_IS_ADDR_LOOPBACK(&ipv6))
      return AddressKind::loopback;
    if (IN6_IS_ADDR_UNSPECIFIED(&ipv6) || IN6_IS_ADDR_MULTICAST(&ipv6) ||
        IN6_IS_ADDR_LINKLOCAL(&ipv6) || IN6_IS_ADDR_V4MAPPED(&ipv6) || IN6_IS_ADDR_V4COMPAT(&ipv6))
      return AddressKind::invalid;
    return AddressKind::mobile;
  }
  return classify_dns_name(normalized);
}
} // namespace

std::string vmix_telestrator_public_url(std::string_view backend_host,
                                        std::uint16_t port,
                                        std::string_view local_lan_address)
{
  if (port == 0)
    return {};
  auto host = backend_host;
  const auto kind = classify_address(host);
  if (kind == AddressKind::invalid)
    return {};
  if (kind == AddressKind::loopback)
    host = local_lan_address;
  if (classify_address(host) != AddressKind::mobile)
    return {};
  const auto authority =
      host.find(':') == std::string_view::npos ? std::string(host) : "[" + std::string(host) + "]";
  // The existing vMix transport/configuration supports HTTP only. Preserve its scheme.
  return "http://" + authority + (port == 80 ? "" : ":" + std::to_string(port)) + "/telestrator/";
}
} // namespace studio_remote::windows
