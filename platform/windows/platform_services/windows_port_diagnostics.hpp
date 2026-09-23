#pragma once

#include "windows_listener_port.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote::windows
{
enum class StartupPortPurpose
{
  companion_listener,
  production_backend
};

struct StartupPortTarget
{
  std::string display_name;
  std::string host;
  std::uint16_t port = 0;
  bool configured = false;
  bool service_available = false;
  StartupPortPurpose purpose = StartupPortPurpose::production_backend;
};

struct StartupPortIssue
{
  std::string display_name;
  std::string endpoint;
  std::uint32_t error_code = 0;
  StartupPortPurpose purpose = StartupPortPurpose::production_backend;
};

struct StartupPortDiagnostics
{
  std::vector<StartupPortIssue> issues;
  std::vector<std::uint16_t> suggested_ports;
  std::string message;
  bool focus_companion_network = false;
};

using ListenerPortProbe = std::function<ListenerPortAvailability(std::uint16_t)>;

/// Returns true for loopback, current computer names, or an address assigned to this host.
bool is_local_production_host(std::string_view host);

/// Inspects configured startup ports without connecting to any production service.
StartupPortDiagnostics inspect_startup_ports(std::span<const StartupPortTarget> targets);

/// Injectable overload used by deterministic platform-policy tests.
StartupPortDiagnostics inspect_startup_ports(std::span<const StartupPortTarget> targets,
                                             const ListenerPortProbe& probe);
} // namespace studio_remote::windows
