#pragma once

#include <studio_remote/ui/management_view.hpp>

#include <map>
#include <optional>
#include <span>
#include <string>

namespace studio_remote
{
struct ManagementClientNotification
{
  std::string title;
  std::string body;
};

/// Frontend-thread-owned baseline of authenticated connected rows. Initial observation is silent;
/// multiple transitions in a snapshot produce one summary. Failed observations must be skipped.
class ManagementClientNotifications
{
public:
  std::optional<ManagementClientNotification> observe(std::span<const ManagementClientRow> clients);

private:
  bool initialized_ = false;
  std::map<std::string, std::string> connected_;
};
} // namespace studio_remote
