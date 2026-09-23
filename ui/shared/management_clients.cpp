#include <studio_remote/ui/management_clients.hpp>

#include <studio_remote/credentials/audit_store.hpp>
#include <studio_remote/provider/provider_catalog.hpp>

#include <algorithm>
#include <compare>
#include <tuple>
#include <unordered_set>

namespace studio_remote
{
namespace
{
std::string production_provider_display_name(std::string_view provider_id)
{
  const auto* provider = find_known_production_provider(provider_id);
  return provider ? std::string(provider->display_name) : std::string(provider_id);
}

std::string last_seen_text(std::uint64_t unix_ms, std::chrono::system_clock::time_point now)
{
  if (unix_ms == 0)
    return "never";
  const auto last_seen = std::chrono::system_clock::time_point(std::chrono::milliseconds(unix_ms));
  const auto seconds = std::max<std::int64_t>(
      0, std::chrono::duration_cast<std::chrono::seconds>(now - last_seen).count());
  if (seconds < 60)
    return "just now";
  const auto minutes = seconds / 60;
  if (minutes < 60)
    return std::to_string(minutes) + (minutes == 1 ? " minute ago" : " minutes ago");
  const auto hours = minutes / 60;
  return std::to_string(hours) + (hours == 1 ? " hour ago" : " hours ago");
}

} // namespace

std::vector<ManagementClient>
make_management_clients(std::span<const Device> paired,
                        std::span<const Device> connected,
                        std::span<const ClientIdentification> identifications)
{
  std::unordered_set<std::string> connected_ids;
  for (const auto& device : connected)
    connected_ids.insert(device.id);
  std::vector<ManagementClient> clients;
  clients.reserve(paired.size());
  for (const auto& device : paired)
  {
    const auto identification =
        std::ranges::find(identifications, device.id, &ClientIdentification::device_id);
    const auto code = connected_ids.contains(device.id) && identification != identifications.end()
                          ? identification->code
                          : std::string{};
    clients.push_back({device.id,
                       device.name,
                       device.role,
                       device.last_seen_unix_ms,
                       connected_ids.contains(device.id),
                       device.production_provider,
                       production_provider_display_name(device.production_provider),
                       device.device_info.type,
                       code});
  }
  return clients;
}

std::vector<ManagementClientRow>
make_management_client_rows(std::vector<ManagementClient> clients,
                            std::chrono::system_clock::time_point now)
{
  std::vector<ManagementClientRow> rows;
  rows.reserve(clients.size());
  for (const auto& client : clients)
  {
    rows.push_back({client.id,
                    client.name,
                    client.production_backend_name,
                    std::string(role_name(client.role)),
                    client.connected ? "now" : last_seen_text(client.last_seen_unix_ms, now),
                    client.connected,
                    client.role == Role::viewer,
                    client.device_type,
                    client.identification_code,
                    client.role == Role::operator_,
                    client.production_provider,
                    client.last_seen_unix_ms,
                    {}});
  }
  sort_management_client_rows(rows, {});
  return rows;
}

void sort_management_client_rows(std::vector<ManagementClientRow>& rows, ManagementClientSort sort)
{
  std::ranges::sort(rows,
                    [sort](const auto& left, const auto& right)
                    {
                      auto order = std::strong_ordering::equal;
                      switch (sort.column)
                      {
                      case ManagementClientColumn::status:
                        order = left.connected <=> right.connected;
                        break;
                      case ManagementClientColumn::client:
                        order = left.name <=> right.name;
                        break;
                      case ManagementClientColumn::backend:
                        order = left.backend <=> right.backend;
                        break;
                      case ManagementClientColumn::role:
                        order = left.role <=> right.role;
                        break;
                      case ManagementClientColumn::last_seen:
                        // A connected client's last seen is now, irrespective of its persisted
                        // timestamp.
                        order = left.connected <=> right.connected;
                        if (order == 0 && !left.connected)
                          order = left.last_seen_unix_ms <=> right.last_seen_unix_ms;
                        break;
                      }
                      if (order != 0)
                        return sort.direction == ManagementSortDirection::ascending ? order < 0
                                                                                    : order > 0;
                      return std::tie(left.name, left.backend, left.id) <
                             std::tie(right.name, right.backend, right.id);
                    });
}

} // namespace studio_remote
