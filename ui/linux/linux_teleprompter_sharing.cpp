#include "linux_management_pages.hpp"
#include "linux_ui_helpers.hpp"

#include <algorithm>
#include <exception>
#include <memory>

namespace studio_remote::linux
{
namespace
{
using SelectionAction = std::function<void()>;
void selection_changed(GObject*, gpointer data) noexcept
{
  try
  {
    const auto action = *static_cast<SelectionAction*>(data);
    action();
  }
  catch (const std::exception& error)
  {
    g_warning("Teleprompter selection failed: %s", error.what());
  }
  catch (...)
  {
    g_warning("Teleprompter selection failed");
  }
}
void dropdown_changed(GObject* object, GParamSpec*, gpointer data) noexcept
{
  selection_changed(object, data);
}
void bind_selection(gpointer source, const char* signal, GCallback callback, SelectionAction action)
{
  auto owned = std::make_unique<SelectionAction>(std::move(action));
  g_signal_connect_data(
      source,
      signal,
      callback,
      owned.release(),
      [](gpointer data, GClosure*) { delete static_cast<SelectionAction*>(data); },
      static_cast<GConnectFlags>(0));
}
std::string player_label(const ManagementClientRow& client)
{
  return client.name + " (" + client.identification_code + ") - " +
         (client.connected ? "Online" : "Offline");
}
GtkWidget* script_choice(LinuxPageContext context, const teleprompter::Summary& script)
{
  auto* check = gtk_check_button_new_with_label(script.name.c_str());
  const auto& selected = context.controller.teleprompter().state().sharing->script_ids;
  gtk_check_button_set_active(GTK_CHECK_BUTTON(check),
                              std::ranges::find(selected, script.id) != selected.end());
  bind_selection(check,
                 "toggled",
                 G_CALLBACK(selection_changed),
                 [context, id = script.id, check]
                 {
                   context.controller.teleprompter().select_shared_script(
                       id, gtk_check_button_get_active(GTK_CHECK_BUTTON(check)));
                   context.redraw();
                 });
  return check;
}
GtkWidget* monitor_choice(LinuxPageContext context, const ManagementClientRow& client)
{
  auto* check = gtk_check_button_new_with_label(player_label(client).c_str());
  const auto& members = context.controller.teleprompter().state().sharing->members;
  const bool selected =
      std::ranges::find(members, client.id, &teleprompter::SessionMember::device_id) !=
      members.end();
  gtk_check_button_set_active(GTK_CHECK_BUTTON(check), selected);
  gtk_widget_set_sensitive(check, selected || members.size() < 7);
  bind_selection(check,
                 "toggled",
                 G_CALLBACK(selection_changed),
                 [context, id = client.id, check]
                 {
                   const bool active = gtk_check_button_get_active(GTK_CHECK_BUTTON(check));
                   context.controller.teleprompter().select_shared_client(id, active);
                   if (active)
                     context.controller.teleprompter().set_shared_role(
                         id, teleprompter::SessionRole::monitor);
                   context.redraw();
                 });
  return check;
}
std::vector<ManagementClientRow> eligible_players(LinuxPageContext context)
{
  const auto& state = context.controller.teleprompter().state();
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
  return eligible;
}
GtkWidget* master_choice(LinuxPageContext context,
                         const std::vector<ManagementClientRow>& eligible,
                         const std::string& master)
{
  // GTK has no ComboBox placeholder. Offer it only until a valid Master is selected.
  auto* labels = gtk_string_list_new(nullptr);
  const auto found = std::ranges::find(eligible, master, &ManagementClientRow::id);
  const guint offset = found == eligible.end() ? 1 : 0;
  if (offset != 0)
    gtk_string_list_append(labels, "Choose a player...");
  guint selected = 0;
  for (std::size_t index = 0; index < eligible.size(); ++index)
  {
    gtk_string_list_append(labels, player_label(eligible[index]).c_str());
    if (eligible[index].id == master)
      selected = static_cast<guint>(index) + offset;
  }
  auto* choice = gtk_drop_down_new(G_LIST_MODEL(labels), nullptr);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(choice), selected);
  bind_selection(choice,
                 "notify::selected",
                 G_CALLBACK(dropdown_changed),
                 [context, eligible, master, choice, offset]
                 {
                   const auto index = gtk_drop_down_get_selected(GTK_DROP_DOWN(choice));
                   if (index < offset || index - offset >= eligible.size())
                     return;
                   if (!master.empty())
                     context.controller.teleprompter().select_shared_client(master, false);
                   const auto& id = eligible[index - offset].id;
                   context.controller.teleprompter().select_shared_client(id, true);
                   context.controller.teleprompter().set_shared_role(
                       id, teleprompter::SessionRole::master);
                   context.redraw();
                 });
  return ui::form_row("Master Player", choice);
}
} // namespace

GtkWidget* make_linux_teleprompter_sharing(LinuxPageContext context)
{
  auto* form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  const auto& state = context.controller.teleprompter().state();
  ui::append(form, ui::section_title("Controller: " + state.client_name));
  ui::append(
      form,
      ui::description(
          "Use this device to choose scripts, start or pause, and change all player settings. "
          "Attached players only display the text."));
  ui::append(
      form,
      ui::description(
          "Using just this device? Cancel this setup and use its private scripts. It is both "
          "Controller and Master Player."));
  ui::append(form, ui::section_title("1. Choose the scripts"));
  if (state.scripts.empty())
    ui::append(
        form,
        ui::description("Attach a library script to this Controller before setting up players."));
  for (const auto& script : state.scripts)
    ui::append(form, script_choice(context, script));
  ui::append(form, ui::section_title("2. Choose the presenter's screen"));
  ui::append(form,
             ui::description(
                 "The Master Player is the screen the presenter reads, usually at the camera. It "
                 "controls the pace and listens when voice following is enabled."));
  const auto eligible = eligible_players(context);
  std::string master;
  for (const auto& member : state.sharing->members)
    if (member.role == teleprompter::SessionRole::master)
      master = member.device_id;
  ui::append(form, master_choice(context, eligible, master));
  if (eligible.empty())
    ui::append(form,
               ui::description("No players available. Pair a device with the same adapter as a "
                               "Viewer, then return here."));
  ui::append(form, ui::section_title("3. Add extra screens (optional)"));
  ui::append(
      form,
      ui::description(
          "Monitors follow the Master Player. They cannot change scripts, pace or settings."));
  for (const auto& client : eligible)
    if (client.id != master)
      ui::append(form, monitor_choice(context, client));
  const auto selected = std::ranges::find(eligible, master, &ManagementClientRow::id);
  ui::append(form, ui::section_title("Ready to start"));
  ui::append(form,
             ui::description(
                 std::to_string(state.sharing->script_ids.size()) + " script(s), " +
                 (selected == eligible.end() ? "No Master selected" : "Master: " + selected->name) +
                 ", " + std::to_string(state.sharing->members.size() - (master.empty() ? 0 : 1)) +
                 " extra screen(s)"));
  ui::append(
      form,
      ui::description(
          "Only " + state.client_name +
          " changes settings. Starting replaces this Controller's current sharing session and "
          "returns the text to the beginning."));
  auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  ui::append(actions,
             ui::button(
                 "Start sharing",
                 [context]
                 {
                   context.controller.teleprompter().save_sharing();
                   context.redraw();
                 },
                 !state.sharing->script_ids.empty() && selected != eligible.end()));
  ui::append(actions,
             ui::button("Cancel",
                        [context]
                        {
                          context.controller.teleprompter().cancel_sharing();
                          context.redraw();
                        }));
  ui::append(form, actions);
  return form;
}

GtkWidget* make_linux_teleprompter_sharing_actions(LinuxPageContext context)
{
  auto* actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  const auto& state = context.controller.teleprompter().state();
  const auto roles = context.controller.teleprompter().shared_roles(state.device_id);
  if (std::ranges::find(roles, teleprompter::SessionRole::controller) == roles.end())
  {
    ui::append(
        actions,
        ui::description(
            "To attach this player, open the Controller in Clients and choose Set up players. "
            "Shared playback and settings are controlled there."));
    return actions;
  }
  ui::append(actions,
             ui::button(state.sessions.empty() ? "Set up players" : "Change players or scripts",
                        [context]
                        {
                          context.controller.teleprompter().configure_sharing();
                          context.redraw();
                        }));
  ui::append(
      actions,
      ui::description(
          state.sessions.empty()
              ? "No sharing active. Private scripts run on this device only."
              : "Sharing is active. This Controller owns playback and all player settings."));
  for (const auto& session : state.sessions)
  {
    if (session.state.owner_device_id != state.device_id)
      continue;
    ui::append(
        actions,
        ui::description(std::to_string(session.scripts.size()) + " shared script(s) - " +
                        std::to_string(session.members.empty() ? 0 : session.members.size() - 1) +
                        " attached player(s)"));
    ui::append(actions,
               ui::button("Stop sharing",
                          [context, id = session.state.id, version = session.state.version]
                          {
                            ui::confirm(context.window,
                                        "Stop sharing",
                                        "End this shared teleprompter session for all its clients?",
                                        [context, id, version]
                                        {
                                          context.controller.teleprompter().stop_sharing(id,
                                                                                         version);
                                          context.redraw();
                                        });
                          }));
  }
  return actions;
}
} // namespace studio_remote::linux
