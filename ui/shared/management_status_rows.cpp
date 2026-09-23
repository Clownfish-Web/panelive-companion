#include <studio_remote/ui/management_status_rows.hpp>

namespace studio_remote
{
namespace
{
std::string health_name(ManagementHealth health)
{
  switch (health)
  {
  case ManagementHealth::healthy:
    return "healthy";
  case ManagementHealth::idle:
    return "idle";
  case ManagementHealth::degraded:
    return "degraded";
  case ManagementHealth::unavailable:
    return "unavailable";
  }
  return "unavailable";
}
} // namespace

std::vector<ManagementStatusRow> make_management_status_rows(const ManagementStatusSnapshot& status)
{
  std::vector<ManagementStatusRow> rows;
  rows.reserve(status.feeds.size());
  for (const auto& feed : status.feeds)
    rows.push_back({feed.name, health_name(feed.health), feed.detail});
  return rows;
}

} // namespace studio_remote
