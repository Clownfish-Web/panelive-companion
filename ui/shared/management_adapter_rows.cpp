#include <studio_remote/ui/management_adapter_rows.hpp>

#include <utility>

namespace studio_remote
{
std::vector<ManagementAdapterRow>
make_management_adapter_rows(const std::vector<AdapterGatewayStatus>& adapters)
{
  std::vector<ManagementAdapterRow> rows;
  rows.reserve(adapters.size());
  for (const auto& adapter : adapters)
  {
    rows.push_back({adapter.provider_id,
                    adapter.display_name,
                    adapter.gateway_running ? "running" : "stopped",
                    adapter.backend_online ? "online" : "offline",
                    adapter.detail,
                    !adapter.gateway_running,
                    adapter.gateway_running,
                    adapter.gateway_running});
  }
  return rows;
}

ManagementObsConnection make_management_obs_connection(const ObsConnectionStatus& status)
{
  auto summary = status.connected && status.synchronized
                     ? "Connected and synchronized with " + status.backend_name
                 : status.connected
                     ? "Connected to " + status.backend_name + "; synchronizing state"
                     : status.backend_name + " is offline";
  if (!status.detail.empty())
    summary += " - " + status.detail;
  return {status.host, status.port, std::move(summary), status.connected};
}

} // namespace studio_remote
