#include "windows_port_diagnostics.hpp"

#include <WS2tcpip.h>
#include <WinSock2.h>
#include <Windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <memory>
#include <set>
#include <sstream>

namespace studio_remote::windows
{
namespace
{
constexpr std::uint16_t suggested_range_first = 16'000;
constexpr std::uint16_t suggested_range_last = 19'999;
constexpr std::size_t suggestion_count = 3;

std::string lowercase(std::string_view value)
{
  std::string result(value);
  std::transform(result.begin(),
                 result.end(),
                 result.begin(),
                 [](unsigned char character)
                 { return static_cast<char>(std::tolower(character)); });
  return result;
}

std::string computer_name(COMPUTER_NAME_FORMAT format)
{
  std::array<char, 256> buffer{};
  DWORD size = static_cast<DWORD>(buffer.size());
  if (!GetComputerNameExA(format, buffer.data(), &size) || size == 0)
    return {};
  return lowercase(std::string_view(buffer.data(), size));
}

std::string endpoint(const StartupPortTarget& target)
{
  if (target.purpose == StartupPortPurpose::companion_listener)
    return "0.0.0.0:" + std::to_string(target.port);
  return target.host + ":" + std::to_string(target.port);
}

bool is_local_numeric_address(std::string_view host)
{
  std::string address_text(host);
  if (address_text.size() > 2 && address_text.front() == '[' && address_text.back() == ']')
    address_text = address_text.substr(1, address_text.size() - 2);

  sockaddr_storage target{};
  int family = AF_INET;
  auto* target_ipv4 = reinterpret_cast<sockaddr_in*>(&target);
  if (InetPtonA(AF_INET, address_text.c_str(), &target_ipv4->sin_addr) != 1)
  {
    family = AF_INET6;
    if (InetPtonA(AF_INET6,
                  address_text.c_str(),
                  &reinterpret_cast<sockaddr_in6*>(&target)->sin6_addr) != 1)
      return false;
  }

  ULONG buffer_size = 0;
  const auto address_flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST;
  if (GetAdaptersAddresses(AF_UNSPEC, address_flags, nullptr, nullptr, &buffer_size) !=
      ERROR_BUFFER_OVERFLOW)
    return false;
  const auto buffer = std::make_unique<unsigned char[]>(buffer_size);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.get());
  if (GetAdaptersAddresses(AF_UNSPEC, address_flags, nullptr, adapters, &buffer_size) != NO_ERROR)
    return false;

  for (auto* adapter = adapters; adapter; adapter = adapter->Next)
  {
    for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next)
    {
      if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != family)
        continue;
      if (family == AF_INET)
      {
        const auto* address = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
        const auto& candidate = address->sin_addr;
        if (std::memcmp(&candidate,
                        &reinterpret_cast<sockaddr_in*>(&target)->sin_addr,
                        sizeof(candidate)) == 0)
          return true;
      }
      else
      {
        const auto& candidate =
            reinterpret_cast<sockaddr_in6*>(unicast->Address.lpSockaddr)->sin6_addr;
        if (std::memcmp(&candidate,
                        &reinterpret_cast<sockaddr_in6*>(&target)->sin6_addr,
                        sizeof(candidate)) == 0)
          return true;
      }
    }
  }
  return false;
}

bool report_issue(const StartupPortTarget& target, const ListenerPortAvailability& availability)
{
  if (target.purpose == StartupPortPurpose::companion_listener)
    return !availability.available;
  return availability.error_code == WSAEACCES;
}

std::vector<std::uint16_t> find_suggestions(const ListenerPortProbe& probe,
                                            const std::set<std::uint16_t>& configured_ports)
{
  std::vector<std::uint16_t> result;
  for (std::uint32_t candidate = suggested_range_first;
       candidate <= suggested_range_last && result.size() < suggestion_count;
       ++candidate)
  {
    const auto port = static_cast<std::uint16_t>(candidate);
    if (configured_ports.contains(port))
      continue;
    if (probe(port).available)
      result.push_back(port);
  }
  return result;
}

std::string format_message(const StartupPortDiagnostics& diagnostics)
{
  std::ostringstream message;
  message << "One or more configured TCP ports required by PaneLive Companion failed "
             "startup checks.\n\n";
  for (const auto& issue : diagnostics.issues)
  {
    message << issue.display_name << ": " << issue.endpoint << "\n";
    if (issue.error_code == WSAEACCES)
    {
      message << "Windows denied an exclusive bind with socket error 10013. The port is in an "
                 "excluded or reserved range, and the expected service is not available there.\n\n";
    }
    else if (issue.error_code == WSAEADDRINUSE)
    {
      message << "Another process already owns this listening port.\n\n";
    }
    else
    {
      message << "The port failed its startup bind check with Windows socket error "
              << issue.error_code << ".\n\n";
    }
  }
  message << "Suggested fixed-port search range: " << suggested_range_first << '-'
          << suggested_range_last << ".";
  if (!diagnostics.suggested_ports.empty())
  {
    message << " Available ports detected now: ";
    for (std::size_t index = 0; index < diagnostics.suggested_ports.size(); ++index)
    {
      if (index > 0)
        message << ", ";
      message << diagnostics.suggested_ports[index];
    }
    message << '.';
  }
  message << "\n\nChange the port in OBS Studio or vMix first, then enter the same port in "
             "Companion. Give every local service a unique port. Availability is a startup "
             "snapshot; verify the chosen port after the next Windows restart with:\n\n"
             "netsh interface ipv4 show excludedportrange protocol=tcp";
  return message.str();
}
} // namespace

bool is_local_production_host(std::string_view host)
{
  const auto normalized = lowercase(host);
  if (normalized == "localhost" || normalized == "::1" || normalized == "[::1]")
    return true;
  if (normalized.starts_with("127."))
    return true;
  const auto netbios = computer_name(ComputerNameNetBIOS);
  const auto dns_host = computer_name(ComputerNameDnsHostname);
  const auto dns_name = computer_name(ComputerNameDnsFullyQualified);
  return (!netbios.empty() && normalized == netbios) ||
         (!dns_host.empty() && normalized == dns_host) ||
         (!dns_name.empty() && normalized == dns_name) || is_local_numeric_address(normalized);
}

StartupPortDiagnostics inspect_startup_ports(std::span<const StartupPortTarget> targets)
{
  return inspect_startup_ports(targets, check_listener_port_availability);
}

StartupPortDiagnostics inspect_startup_ports(std::span<const StartupPortTarget> targets,
                                             const ListenerPortProbe& probe)
{
  StartupPortDiagnostics result;
  std::set<std::uint16_t> configured_ports;
  for (const auto& target : targets)
  {
    if (!target.configured || target.port == 0)
      continue;
    configured_ports.insert(target.port);
    if (target.service_available)
      continue;
    if (target.purpose == StartupPortPurpose::production_backend &&
        !is_local_production_host(target.host))
      continue;

    const auto availability = probe(target.port);
    if (!report_issue(target, availability))
      continue;
    result.issues.push_back(
        {target.display_name, endpoint(target), availability.error_code, target.purpose});
    if (target.purpose == StartupPortPurpose::companion_listener)
      result.focus_companion_network = true;
  }
  if (result.issues.empty())
    return result;
  result.suggested_ports = find_suggestions(probe, configured_ports);
  result.message = format_message(result);
  return result;
}
} // namespace studio_remote::windows
