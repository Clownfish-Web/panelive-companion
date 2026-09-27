#include "linux_desktop_status.hpp"

#include <algorithm>

namespace studio_remote::linux
{
LinuxDesktopStatus make_linux_desktop_status(const ManagementUiState& state)
{
  LinuxDesktopStatus result;
  const auto online = std::ranges::count_if(state.clients, &ManagementClientRow::connected);
  result.title = "PaneLive - " + std::to_string(online) + " online";
  const auto& network = state.network_settings;
  result.attention = !network.listening || !network.warning.empty();
  result.lines.push_back(network.listening
                             ? "Listening: " + network.address + ":" + std::to_string(network.port)
                             : "Listener: unavailable - open Companion settings");
  result.lines.push_back("Clients: " + std::to_string(online) + " online / " +
                         std::to_string(state.clients.size()) + " paired");
  for (const auto& adapter : state.adapters)
  {
    result.lines.push_back(adapter.name + ": " + adapter.gateway_state + " / " +
                           adapter.backend_state);
  }
  for (const auto& feed : state.status_feeds)
  {
    result.lines.push_back(feed.name + ": " + feed.state);
    result.attention = result.attention || feed.state == "degraded";
  }
  if (!state.last_error.empty())
  {
    result.lines.push_back("Management warning: open Companion for details");
    result.attention = true;
  }
  return result;
}
} // namespace studio_remote::linux
