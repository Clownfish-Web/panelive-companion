#pragma once

#include <string>
#include <studio_remote/ui/management_status.hpp>
#include <studio_remote/ui/management_view.hpp>
#include <vector>

namespace studio_remote
{
struct AdapterGatewayStatus
{
  std::string provider_id;
  std::string display_name;
  bool gateway_running = false;
  bool backend_online = false;
  std::string detail;
};

enum class AdapterGatewayAction
{
  start,
  stop,
  restart
};

std::vector<ManagementAdapterRow>
make_management_adapter_rows(const std::vector<AdapterGatewayStatus>& adapters);

ManagementObsConnection make_management_obs_connection(const ObsConnectionStatus& status);
} // namespace studio_remote
