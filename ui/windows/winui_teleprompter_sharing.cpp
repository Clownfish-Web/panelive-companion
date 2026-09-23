#include "winui_management_pages.hpp"

#include <algorithm>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace
{
controls::CheckBox script_choice(WinUiPageContext context, const teleprompter::Summary& script)
{
  controls::CheckBox check;
  check.Name(L"SharedTeleprompterScriptChoice");
  check.Content(winrt::box_value(wide(script.name)));
  const auto& selected = context.controller.teleprompter().state().sharing->script_ids;
  check.IsChecked(std::ranges::find(selected, script.id) != selected.end());
  const auto changed = [context, id = script.id](const auto& sender, const auto&)
  {
    context.controller.teleprompter().select_shared_script(
        id, sender.template as<controls::CheckBox>().IsChecked().Value());
    context.redraw();
  };
  check.Checked(changed);
  check.Unchecked(changed);
  return check;
}

std::string player_label(const ManagementClientRow& client)
{
  return client.name + " (" + client.identification_code + ") - " +
         (client.connected ? "Online" : "Offline");
}

controls::CheckBox monitor_choice(WinUiPageContext context, const ManagementClientRow& client)
{
  controls::CheckBox check;
  check.Name(L"SharedTeleprompterClientChoice");
  check.Content(winrt::box_value(wide(player_label(client))));
  const auto& members = context.controller.teleprompter().state().sharing->members;
  const auto found = std::ranges::find(members, client.id, &teleprompter::SessionMember::device_id);
  check.IsChecked(found != members.end());
  check.IsEnabled(found != members.end() || members.size() < 7);
  const auto changed = [context, id = client.id](const auto& sender, const auto&)
  {
    const auto selected = sender.template as<controls::CheckBox>().IsChecked().Value();
    context.controller.teleprompter().select_shared_client(id, selected);
    if (selected)
      context.controller.teleprompter().set_shared_role(id, teleprompter::SessionRole::monitor);
    context.redraw();
  };
  check.Checked(changed);
  check.Unchecked(changed);
  return check;
}
} // namespace

xaml::FrameworkElement make_teleprompter_sharing(WinUiPageContext context)
{
  controls::StackPanel form;
  form.Spacing(12);
  const auto& state = context.controller.teleprompter().state();
  const auto add_text = [&](std::string text, bool heading = false)
  {
    auto label = make_table_text(text, !heading, heading);
    label.TextWrapping(xaml::TextWrapping::Wrap);
    form.Children().Append(label);
  };
  add_text("Controller: " + state.client_name, true);
  add_text("Use this device to choose scripts, start or pause, and change all player settings. "
           "Attached players only display the text.");
  add_text("Using just this device? Cancel this setup and use its private scripts. It is both "
           "Controller and Master Player.");
  add_text("1. Choose the scripts", true);
  if (state.scripts.empty())
    add_text("Attach a library script to this Controller before setting up players.");
  for (const auto& script : state.scripts)
    form.Children().Append(script_choice(context, script));
  add_text("2. Choose the presenter's screen", true);
  add_text("The Master Player is the screen the presenter reads, usually at the camera. It "
           "controls the pace and listens when voice following is enabled.");
  const auto& clients = context.controller.state().clients;
  const auto owner = std::ranges::find(clients, state.device_id, &ManagementClientRow::id);
  std::vector<ManagementClientRow> eligible;
  if (owner != clients.end())
    for (const auto& client : clients)
    {
      const auto roles = context.controller.teleprompter().shared_roles(client.id);
      if (client.id != state.device_id && client.backend == owner->backend &&
          std::ranges::find(roles, teleprompter::SessionRole::master) != roles.end())
        eligible.push_back(client);
    }
  std::string master;
  for (const auto& member : state.sharing->members)
    if (member.role == teleprompter::SessionRole::master)
      master = member.device_id;
  controls::ComboBox choice;
  choice.Name(L"SharedTeleprompterMasterPlayer");
  choice.Header(winrt::box_value(L"Master Player"));
  choice.PlaceholderText(L"Choose a player...");
  for (const auto& client : eligible)
    choice.Items().Append(winrt::box_value(wide(player_label(client))));
  const auto selected = std::ranges::find(eligible, master, &ManagementClientRow::id);
  choice.SelectedIndex(selected == eligible.end() ? -1
                                                  : static_cast<int>(selected - eligible.begin()));
  choice.SelectionChanged(
      [context, eligible, master](const auto& sender, const auto&)
      {
        const auto index = sender.template as<controls::ComboBox>().SelectedIndex();
        if (index < 0 || static_cast<std::size_t>(index) >= eligible.size())
          return;
        if (!master.empty())
          context.controller.teleprompter().select_shared_client(master, false);
        const auto& id = eligible[static_cast<std::size_t>(index)].id;
        context.controller.teleprompter().select_shared_client(id, true);
        context.controller.teleprompter().set_shared_role(id, teleprompter::SessionRole::master);
        context.redraw();
      });
  form.Children().Append(choice);
  if (eligible.empty())
    add_text(
        "No players available. Pair a device with the same adapter as a Viewer, then return here.");
  add_text("3. Add extra screens (optional)", true);
  add_text("Monitors follow the Master Player. They cannot change scripts, pace or settings.");
  for (const auto& client : eligible)
    if (client.id != master)
      form.Children().Append(monitor_choice(context, client));
  add_text("Ready to start", true);
  add_text(std::to_string(state.sharing->script_ids.size()) + " script(s), " +
           (selected == eligible.end() ? "No Master selected" : "Master: " + selected->name) +
           ", " + std::to_string(state.sharing->members.size() - (master.empty() ? 0 : 1)) +
           " extra screen(s)");
  add_text("Only " + state.client_name +
           " changes settings. Starting replaces this Controller's current sharing session and "
           "returns the text to the beginning.");
  controls::StackPanel actions;
  actions.Orientation(controls::Orientation::Horizontal);
  actions.Spacing(8);
  controls::Button save;
  save.Name(L"SaveTeleprompterSharing");
  save.Content(winrt::box_value(L"Start sharing"));
  save.IsEnabled(!state.sharing->script_ids.empty() && selected != eligible.end());
  save.Click(
      [context](const auto&, const auto&)
      {
        context.controller.teleprompter().save_sharing();
        context.redraw();
      });
  controls::Button cancel;
  cancel.Content(winrt::box_value(L"Cancel"));
  cancel.Click(
      [context](const auto&, const auto&)
      {
        context.controller.teleprompter().cancel_sharing();
        context.redraw();
      });
  actions.Children().Append(save);
  actions.Children().Append(cancel);
  form.Children().Append(actions);
  return form;
}

xaml::FrameworkElement make_teleprompter_sharing_actions(WinUiPageContext context)
{
  controls::StackPanel actions;
  actions.Spacing(8);
  const auto& state = context.controller.teleprompter().state();
  const auto roles = context.controller.teleprompter().shared_roles(state.device_id);
  if (std::ranges::find(roles, teleprompter::SessionRole::controller) == roles.end())
  {
    auto message =
        make_table_text("To attach this player, open the Controller in Clients and choose Set up "
                        "players. Shared playback and settings are controlled there.",
                        true);
    message.TextWrapping(xaml::TextWrapping::Wrap);
    actions.Children().Append(message);
    return actions;
  }
  controls::Button configure;
  configure.Name(L"ConfigureTeleprompterSharing");
  configure.Content(
      winrt::box_value(state.sessions.empty() ? L"Set up players" : L"Change players or scripts"));
  configure.Click(
      [context](const auto&, const auto&)
      {
        context.controller.teleprompter().configure_sharing();
        context.redraw();
      });
  actions.Children().Append(configure);
  auto status = make_table_text(
      state.sessions.empty()
          ? "No sharing active. Private scripts run on this device only."
          : "Sharing is active. This Controller owns playback and all player settings.",
      true);
  status.TextWrapping(xaml::TextWrapping::Wrap);
  actions.Children().Append(status);
  for (const auto& session : state.sessions)
  {
    if (session.state.owner_device_id != state.device_id)
      continue;
    actions.Children().Append(make_table_text(
        std::to_string(session.scripts.size()) + " shared script(s) - " +
            std::to_string(session.members.empty() ? 0 : session.members.size() - 1) +
            " attached player(s)",
        true));
    controls::Button stop;
    stop.Name(L"StopTeleprompterSharing");
    stop.Content(winrt::box_value(L"Stop sharing"));
    stop.Click(
        [context, id = session.state.id, version = session.state.version](const auto&, const auto&)
        {
          context.confirm(L"Stop sharing",
                          L"End this shared teleprompter session for all its clients?",
                          [context, id, version]
                          {
                            context.controller.teleprompter().stop_sharing(id, version);
                            context.redraw();
                          });
        });
    actions.Children().Append(stop);
  }
  return actions;
}
} // namespace studio_remote::windows
