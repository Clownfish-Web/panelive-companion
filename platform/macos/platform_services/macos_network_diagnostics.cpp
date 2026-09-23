#include "macos_network_diagnostics.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace studio_remote::macos
{
namespace
{
using InterfaceTypes = std::map<unsigned int, std::string>;

InterfaceTypes interface_types(json& errors)
{
  std::unique_ptr<const __CFArray, decltype(&CFRelease)> interfaces(SCNetworkInterfaceCopyAll(),
                                                                    CFRelease);
  if (!interfaces)
  {
    errors.push_back("networkHardwareTypesUnavailable");
    return {};
  }
  InterfaceTypes result;
  for (CFIndex index = 0; index < CFArrayGetCount(interfaces.get()); ++index)
  {
    const auto interface =
        static_cast<SCNetworkInterfaceRef>(CFArrayGetValueAtIndex(interfaces.get(), index));
    const auto name = SCNetworkInterfaceGetBSDName(interface);
    const auto type = SCNetworkInterfaceGetInterfaceType(interface);
    std::array<char, IF_NAMESIZE> text{};
    if (!name || !type ||
        !CFStringGetCString(name, text.data(), text.size(), kCFStringEncodingUTF8))
      continue;
    const auto interface_index = if_nametoindex(text.data());
    if (interface_index == 0)
      continue;
    if (CFEqual(type, kSCNetworkInterfaceTypeIEEE80211))
      result[interface_index] = "wifi";
    else if (CFEqual(type, kSCNetworkInterfaceTypeEthernet))
      result[interface_index] = "ethernet";
    else if (CFEqual(type, kSCNetworkInterfaceTypePPP))
      result[interface_index] = "ppp";
  }
  return result;
}

std::string interface_type(std::uint8_t type)
{
  switch (type)
  {
  case IFT_ETHER:
    return "ethernet";
  case IFT_PPP:
    return "ppp";
  case IFT_GIF:
  case IFT_STF:
    return "tunnel";
  default:
    return "other";
  }
}

std::vector<std::byte> interface_table(json& errors)
{
  std::array<int, 6> query{CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, 0};
  constexpr std::size_t maximum_table_bytes = 4 * 1024 * 1024;
  for (int attempt = 0; attempt < 3; ++attempt)
  {
    std::size_t size = 0;
    if (sysctl(query.data(), query.size(), nullptr, &size, nullptr, 0) != 0 ||
        size > maximum_table_bytes)
      break;
    if (size == 0)
      return {};
    std::vector<std::byte> bytes(size);
    if (sysctl(query.data(), query.size(), bytes.data(), &size, nullptr, 0) == 0)
    {
      if (size > bytes.size())
        break;
      bytes.resize(size);
      return bytes;
    }
    if (errno != ENOMEM)
      break;
  }
  errors.push_back("networkCountersUnavailable");
  return {};
}

json interface_snapshot(const if_msghdr2& header, const InterfaceTypes& types)
{
  const auto& data = header.ifm_data;
  const auto type = types.find(header.ifm_index);
  return {{"interfaceIndex", header.ifm_index},
          {"type", type != types.end() ? type->second : interface_type(data.ifi_type)},
          {"connected", (header.ifm_flags & IFF_RUNNING) != 0},
          {"transmitLinkBitsPerSecond", data.ifi_baudrate},
          {"receiveLinkBitsPerSecond", data.ifi_baudrate},
          {"outOctets", data.ifi_obytes},
          {"inOctets", data.ifi_ibytes},
          {"outErrors", data.ifi_oerrors},
          {"inErrors", data.ifi_ierrors},
          {"outDiscards", std::max(header.ifm_snd_drops, 0)},
          {"inDiscards", data.ifi_iqdrops}};
}
} // namespace

json macos_network_diagnostics(json& errors)
{
  const auto types = interface_types(errors);
  const auto bytes = interface_table(errors);
  json result = json::array();
  for (std::size_t offset = 0; offset < bytes.size();)
  {
    std::uint16_t length = 0;
    constexpr std::size_t prefix_size = 4;
    if (bytes.size() - offset < prefix_size)
    {
      errors.push_back("networkCounterLayoutInvalid");
      break;
    }
    std::memcpy(&length, bytes.data() + offset, sizeof(length));
    const auto version = std::to_integer<std::uint8_t>(bytes[offset + 2]);
    const auto type = std::to_integer<std::uint8_t>(bytes[offset + 3]);
    if (length < prefix_size || length > bytes.size() - offset || version != RTM_VERSION)
    {
      errors.push_back("networkCounterLayoutInvalid");
      break;
    }
    if (type == RTM_IFINFO2)
    {
      if (length < sizeof(if_msghdr2))
      {
        errors.push_back("networkCounterLayoutInvalid");
        break;
      }
      if_msghdr2 header{};
      std::memcpy(&header, bytes.data() + offset, sizeof(header));
      if (!(header.ifm_flags & IFF_LOOPBACK) && (header.ifm_flags & IFF_UP))
        result.push_back(interface_snapshot(header, types));
    }
    offset += length;
  }
  return result;
}
} // namespace studio_remote::macos
