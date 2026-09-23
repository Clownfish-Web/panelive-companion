#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace studio_remote
{
enum class ManagementHealth
{
  healthy,
  idle,
  degraded,
  unavailable
};

struct ManagementFeedStatus
{
  std::string name;
  ManagementHealth health = ManagementHealth::unavailable;
  std::string detail;
};

struct ManagementStatusSnapshot
{
  std::vector<ManagementFeedStatus> feeds;
};

struct CompanionEndpointStatus
{
  std::string name;
  std::uint16_t port = 0;
  std::string listening_ports;
};

struct ObsConnectionStatus
{
  std::string host;
  std::uint16_t port = 4455;
  bool connected = false;
  bool synchronized = false;
  std::string detail;
  std::string backend_name = "OBS Studio";
  bool native_helper_required = true;
};
} // namespace studio_remote
