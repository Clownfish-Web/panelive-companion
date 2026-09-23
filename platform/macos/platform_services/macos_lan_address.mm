#include "macos_lan_address.hpp"

#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>

#include <algorithm>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <limits>
#include <memory>
#include <net/if.h>
#include <stdexcept>
#include <studio_remote/discovery/lan_address_selection.hpp>
#include <unordered_map>

namespace studio_remote::macos
{
namespace
{
struct InterfacePreference
{
  bool physical = false;
  bool has_gateway = false;
  std::uint32_t metric = std::numeric_limits<std::uint32_t>::max();
};
using Preferences = std::unordered_map<std::string, InterfacePreference>;
using Interfaces = std::unique_ptr<ifaddrs, decltype(&freeifaddrs)>;

Interfaces active_interfaces()
{
  ifaddrs* raw = nullptr;
  if (::getifaddrs(&raw) != 0)
    throw std::runtime_error("Could not enumerate macOS network interfaces");
  return Interfaces(raw, freeifaddrs);
}

bool active(const ifaddrs& entry)
{
  return entry.ifa_addr && entry.ifa_name && (entry.ifa_flags & IFF_UP) &&
         (entry.ifa_flags & IFF_RUNNING) && !(entry.ifa_flags & IFF_LOOPBACK);
}

std::array<std::uint8_t, 4> ipv4_bytes(const sockaddr* address)
{
  const auto value = ntohl(reinterpret_cast<const sockaddr_in*>(address)->sin_addr.s_addr);
  return {static_cast<std::uint8_t>(value >> 24),
          static_cast<std::uint8_t>(value >> 16),
          static_cast<std::uint8_t>(value >> 8),
          static_cast<std::uint8_t>(value)};
}

std::string address_text(const sockaddr* address)
{
  char text[INET6_ADDRSTRLEN]{};
  const void* bytes =
      address->sa_family == AF_INET
          ? static_cast<const void*>(&reinterpret_cast<const sockaddr_in*>(address)->sin_addr)
          : static_cast<const void*>(&reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr);
  if (!::inet_ntop(address->sa_family, bytes, text, sizeof(text)))
    throw std::runtime_error("Could not format macOS interface address");
  return text;
}

Preferences physical_interfaces()
{
  NSArray* interfaces = CFBridgingRelease(SCNetworkInterfaceCopyAll());
  if (!interfaces)
    throw std::runtime_error("Could not inspect macOS network hardware");
  Preferences result;
  for (id object in interfaces)
  {
    const auto interface = (__bridge SCNetworkInterfaceRef)object;
    const auto type = SCNetworkInterfaceGetInterfaceType(interface);
    NSString* name = (__bridge NSString*)SCNetworkInterfaceGetBSDName(interface);
    if (name.UTF8String && type)
      result[name.UTF8String].physical = CFEqual(type, kSCNetworkInterfaceTypeEthernet) ||
                                         CFEqual(type, kSCNetworkInterfaceTypeIEEE80211);
  }
  return result;
}

void apply_service_preferences(Preferences& preferences)
{
  std::unique_ptr<const __SCDynamicStore, decltype(&CFRelease)> store(
      SCDynamicStoreCreate(nullptr, CFSTR("panelive-companion"), nullptr, nullptr),
      CFRelease);
  if (!store)
    throw std::runtime_error("Could not inspect macOS network service order");
  NSDictionary* values = CFBridgingRelease(SCDynamicStoreCopyMultiple(
      store.get(),
      (__bridge CFArrayRef) @[ @"Setup:/Network/Global/IPv4", @"State:/Network/Global/IPv4" ],
      (__bridge CFArrayRef) @[ @"State:/Network/Service/.*/IPv4" ]));
  if (![values isKindOfClass:NSDictionary.class])
    throw std::runtime_error("Could not read macOS network service state");
  id setup = values[@"Setup:/Network/Global/IPv4"];
  id global = values[@"State:/Network/Global/IPv4"];
  NSArray* order = [setup isKindOfClass:NSDictionary.class] ? setup[@"ServiceOrder"] : nil;
  NSString* primary = [global isKindOfClass:NSDictionary.class] ? global[@"PrimaryInterface"] : nil;
  for (NSString* path in values)
  {
    if (![path isKindOfClass:NSString.class] || ![path hasPrefix:@"State:/Network/Service/"])
      continue;
    NSDictionary* service = values[path];
    if (![service isKindOfClass:NSDictionary.class])
      continue;
    NSString* name = service[@"InterfaceName"];
    if (![name isKindOfClass:NSString.class] || !name.UTF8String)
      continue;
    auto& preference = preferences[name.UTF8String];
    NSString* router = service[@"Router"];
    preference.has_gateway |= [router isKindOfClass:NSString.class] && router.length > 0;
    const auto components = [path componentsSeparatedByString:@"/"];
    if (components.count != 5)
      continue;
    const auto index =
        [order isKindOfClass:NSArray.class] ? [order indexOfObject:components[3]] : NSNotFound;
    if (index != NSNotFound && index < std::numeric_limits<std::uint32_t>::max() - 1)
      preference.metric = std::min(preference.metric, static_cast<std::uint32_t>(index + 1));
  }
  if ([primary isKindOfClass:NSString.class] && primary.UTF8String)
    preferences[primary.UTF8String].metric = 0;
}
} // namespace

std::vector<std::string> available_lan_ipv4_addresses()
{
  @autoreleasepool
  {
    auto preferences = physical_interfaces();
    apply_service_preferences(preferences);
    const auto interfaces = active_interfaces();
    std::vector<LanAddressCandidate> candidates;
    for (auto* entry = interfaces.get(); entry; entry = entry->ifa_next)
    {
      if (!active(*entry) || entry->ifa_addr->sa_family != AF_INET ||
          !is_private_lan_ipv4(ipv4_bytes(entry->ifa_addr)))
        continue;
      const auto preference = preferences[entry->ifa_name];
      candidates.push_back({address_text(entry->ifa_addr),
                            preference.physical,
                            preference.has_gateway,
                            preference.metric});
    }
    return ordered_lan_ipv4_addresses(std::move(candidates));
  }
}

std::string listener_specification(std::uint16_t port, bool allow_non_private_interfaces)
{
  if (!port)
    throw std::invalid_argument("Gateway port is out of range");
  const auto suffix = ":" + std::to_string(port) + "s";
  if (allow_non_private_interfaces)
    return std::to_string(port) + "s";
  std::vector<std::string> listeners;
  const auto interfaces = active_interfaces();
  for (auto* entry = interfaces.get(); entry; entry = entry->ifa_next)
  {
    if (!active(*entry))
      continue;
    if (entry->ifa_addr->sa_family == AF_INET)
    {
      const auto bytes = ipv4_bytes(entry->ifa_addr);
      if (is_private_listener_ipv4(bytes))
        listeners.push_back(address_text(entry->ifa_addr) + suffix);
    }
    else if (entry->ifa_addr->sa_family == AF_INET6)
    {
      const auto* address = reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr);
      if (is_private_listener_ipv6(
              std::span<const std::uint8_t, 16>(address->sin6_addr.s6_addr, 16)))
        listeners.push_back("[" + address_text(entry->ifa_addr) + "]" + suffix);
    }
  }
  std::ranges::sort(listeners);
  listeners.erase(std::unique(listeners.begin(), listeners.end()), listeners.end());
  std::string result = "127.0.0.1" + suffix;
  for (const auto& listener : listeners)
    result += "," + listener;
  return result;
}
} // namespace studio_remote::macos
