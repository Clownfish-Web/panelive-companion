#include <studio_remote/ui/management_view.hpp>
#include <studio_remote/ui/teleprompter_controller.hpp>

#include <algorithm>
#include <utility>

namespace studio_remote
{
void TeleprompterUiController::update_clients(std::span<const ManagementClientRow> clients)
{
  decltype(state_.role_choices) choices;
  for (const auto& client : clients)
  {
    using enum teleprompter::SessionRole;
    if (client.production_provider == "companion-teleprompter")
    {
      if (client.role == "operator")
        choices[client.id] = {controller};
      else if (client.role == "viewer")
        choices[client.id] = {master, monitor};
      else
        choices[client.id] = {};
    }
    else if (client.production_provider == "obs" || client.production_provider == "vmix" ||
             client.production_provider == "streamlabs")
      choices[client.id] = {master, controller, monitor};
  }
  if (choices == state_.role_choices)
    return;
  state_.role_choices = std::move(choices);
  if (!state_.sharing)
    return;
  const auto owner_choices = shared_roles(state_.device_id);
  if (std::ranges::find(owner_choices, teleprompter::SessionRole::controller) ==
      owner_choices.end())
  {
    state_.sharing.reset();
    state_.error = "Sharing setup closed because this device is no longer the Controller";
    ++state_.form_generation;
    return;
  }
  const auto reconcile = [&](std::string_view id, teleprompter::SessionRole& role)
  {
    const auto allowed = shared_roles(id);
    if (std::ranges::find(allowed, role) == allowed.end())
      role = default_shared_role(id);
  };
  reconcile(state_.device_id, state_.sharing->owner_role);
  for (auto& member : state_.sharing->members)
    reconcile(member.device_id, member.role);
  ++state_.form_generation;
}

std::vector<teleprompter::SessionRole>
TeleprompterUiController::shared_roles(std::string_view device_id) const
{
  const auto found = state_.role_choices.find(device_id);
  return found == state_.role_choices.end() ? std::vector<teleprompter::SessionRole>{}
                                            : found->second;
}

teleprompter::SessionRole
TeleprompterUiController::default_shared_role(std::string_view device_id) const
{
  const auto allowed = shared_roles(device_id);
  return allowed.size() == 1 ? allowed.front() : teleprompter::SessionRole::monitor;
}

void TeleprompterUiController::configure_sharing()
{
  if (state_.device_id.empty() || !repository_ || !repository_->shared_sessions() || state_.editor)
    return;
  const auto roles = shared_roles(state_.device_id);
  if (std::ranges::find(roles, teleprompter::SessionRole::controller) == roles.end())
  {
    state_.error =
        "Set up players from the Controller's entry in Clients. This device is a player.";
    return;
  }
  state_.sharing.emplace();
  state_.sharing->owner_role = teleprompter::SessionRole::controller;
  state_.error.clear();
  for (const auto& session : state_.sessions)
  {
    if (session.state.owner_device_id != state_.device_id)
      continue;
    for (const auto& script : session.scripts)
      state_.sharing->script_ids.push_back(script.id);
    for (const auto& member : session.members)
      if (member.device_id != state_.device_id)
        state_.sharing->members.push_back(member);
  }
  if (state_.sharing->script_ids.empty() && state_.scripts.size() == 1)
    state_.sharing->script_ids.push_back(state_.scripts.front().id);
  ++state_.form_generation;
}

void TeleprompterUiController::select_shared_script(std::string script_id, bool selected)
{
  if (!state_.sharing)
    return;
  auto& scripts = state_.sharing->script_ids;
  std::erase(scripts, script_id);
  if (selected)
    scripts.push_back(std::move(script_id));
}

void TeleprompterUiController::select_shared_client(std::string device_id, bool selected)
{
  if (!state_.sharing)
    return;
  auto& members = state_.sharing->members;
  std::erase_if(members, [&](const auto& member) { return member.device_id == device_id; });
  const auto roles = shared_roles(device_id);
  if (selected && device_id != state_.device_id &&
      std::ranges::find(roles, teleprompter::SessionRole::monitor) != roles.end())
  {
    const auto role = default_shared_role(device_id);
    members.push_back({std::move(device_id), role});
  }
}

void TeleprompterUiController::set_shared_role(std::string_view device_id,
                                               teleprompter::SessionRole role)
{
  if (!state_.sharing)
    return;
  const auto allowed = shared_roles(device_id);
  if (std::ranges::find(allowed, role) == allowed.end() ||
      (device_id == state_.device_id && role != teleprompter::SessionRole::controller) ||
      (device_id != state_.device_id && role == teleprompter::SessionRole::controller))
  {
    state_.error = "This client production role does not permit the selected teleprompter role";
    return;
  }
  state_.error.clear();
  if (role == teleprompter::SessionRole::master)
    for (auto& member : state_.sharing->members)
      if (member.device_id != device_id && member.role == teleprompter::SessionRole::master)
        member.role = teleprompter::SessionRole::monitor;
  if (device_id == state_.device_id)
    state_.sharing->owner_role = role;
  else
    for (auto& member : state_.sharing->members)
      if (member.device_id == device_id)
        member.role = role;
}

void TeleprompterUiController::save_sharing()
{
  if (!state_.sharing || !repository_ || !repository_->shared_sessions())
    return;
  if (state_.sharing->script_ids.empty())
  {
    state_.error = "Choose at least one script to show on your players";
    return;
  }
  if (std::ranges::count(state_.sharing->members,
                         teleprompter::SessionRole::master,
                         &teleprompter::SessionMember::role) != 1)
  {
    state_.error = "Choose one Master Player - the screen the presenter reads";
    return;
  }
  const auto result = repository_->shared_sessions()->assign(state_.device_id, *state_.sharing);
  if (!result)
  {
    state_.error = result.message;
    return;
  }
  cancel_sharing();
  refresh();
}

void TeleprompterUiController::cancel_sharing() noexcept
{
  state_.sharing.reset();
  state_.error.clear();
  ++state_.form_generation;
}

void TeleprompterUiController::stop_sharing(std::string_view session_id,
                                            std::uint64_t expected_version)
{
  if (!repository_ || !repository_->shared_sessions())
    return;
  const auto result =
      repository_->shared_sessions()->unassign(state_.device_id, session_id, expected_version);
  refresh();
  state_.error = result ? std::string{} : result.message;
}
} // namespace studio_remote
