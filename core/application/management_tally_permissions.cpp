#include <studio_remote/ui/management_controller.hpp>

#include <algorithm>
#include <exception>

namespace studio_remote
{
void ManagementUiController::refresh_remote_tally_permissions()
{
  std::string permission_error;
  for (auto& client : state_.clients)
  {
    client.remote_tally_allowed.reset();
    if (client.production_provider != "obs" || !callbacks_.remote_tally.permission)
      continue;
    try
    {
      const auto result = callbacks_.remote_tally.permission(client.id);
      if (const auto* allowed = std::get_if<bool>(&result))
        client.remote_tally_allowed = *allowed;
      else if (permission_error.empty())
        permission_error = std::get<tally::PublicationFailure>(result).reason;
    }
    catch (const std::exception& error)
    {
      if (permission_error.empty())
        permission_error = error.what();
    }
  }
  if (!permission_error.empty())
    state_.last_error += (state_.last_error.empty() ? "" : "\n") +
                         std::string("Could not read web tally permission: ") + permission_error;
}

void ManagementUiController::change_remote_tally_permission(
    std::string_view device_id,
    const std::function<tally::PublicationResult<bool>(std::string_view)>& action)
{
  const auto client = std::ranges::find(state_.clients, device_id, &ManagementClientRow::id);
  if (client == state_.clients.end() || client->production_provider != "obs" ||
      !client->remote_tally_allowed.has_value() || !action)
  {
    state_.last_error = "Web tally permission is unavailable for this client";
    return;
  }
  std::string operation_error;
  run_and_refresh(
      [&]
      {
        const auto result = action(device_id);
        if (const auto* failure = std::get_if<tally::PublicationFailure>(&result))
          operation_error = "Could not change web tally permission: " + failure->reason;
      });
  if (!operation_error.empty())
    state_.last_error = operation_error +
                        (state_.last_error.empty() ? "" : "\nRefresh failed: " + state_.last_error);
}

void ManagementUiController::allow_remote_tally(std::string_view device_id)
{
  change_remote_tally_permission(device_id, callbacks_.remote_tally.allow);
}

void ManagementUiController::withdraw_remote_tally(std::string_view device_id)
{
  change_remote_tally_permission(device_id, callbacks_.remote_tally.withdraw);
}
} // namespace studio_remote
