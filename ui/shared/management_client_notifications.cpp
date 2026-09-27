#include <studio_remote/ui/management_client_notifications.hpp>

#include <utility>
#include <vector>

namespace studio_remote
{
std::optional<ManagementClientNotification>
ManagementClientNotifications::observe(std::span<const ManagementClientRow> clients)
{
  std::map<std::string, std::string> current;
  for (const auto& client : clients)
    if (client.connected)
      current.emplace(client.id, client.name.empty() ? "Client" : client.name);

  std::vector<std::string> joined;
  std::vector<std::string> left;
  if (initialized_)
  {
    for (const auto& [id, name] : current)
      if (!connected_.contains(id))
        joined.push_back(name);
    for (const auto& [id, name] : connected_)
      if (!current.contains(id))
        left.push_back(name);
  }
  initialized_ = true;
  connected_ = std::move(current);
  if (joined.empty() && left.empty())
    return std::nullopt;

  ManagementClientNotification result;
  if (joined.size() == 1 && left.empty())
    result = {"Client connected", joined.front() + " connected to PaneLive Companion."};
  else if (left.size() == 1 && joined.empty())
    result = {"Client disconnected", left.front() + " disconnected from PaneLive Companion."};
  else
    result = {"Client connections changed",
              std::to_string(joined.size()) + " connected, " + std::to_string(left.size()) +
                  " disconnected. " + std::to_string(connected_.size()) + " now online."};
  return result;
}

} // namespace studio_remote
