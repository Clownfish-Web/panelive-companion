#include "linux_management_pages.hpp"

#include "linux_clients_toolbar.hpp"
#include "linux_pairing_qr.hpp"
#include "linux_ui_helpers.hpp"

#include <algorithm>
#include <memory>
#include <tuple>

namespace studio_remote::linux
{
namespace
{
constexpr auto snapshot_key = "panelive-clients-snapshot";

struct ClientsSnapshot
{
  std::vector<ManagementClientRow> clients;
  std::string selection;
  std::string pairing_payload;
  int window_width;
  int content_width;
};

auto client_structure(const ManagementClientRow& client)
{
  // Connection, last-seen and identification updates do not change action callbacks.
  return std::tie(client.id, client.name, client.backend, client.role, client.device_type,
                  client.can_set_operator, client.can_set_viewer, client.attached_script_count,
                  client.production_provider, client.remote_tally_allowed);
}

void remember_clients(GtkWidget* page, LinuxPageContext context)
{
  const auto& state = context.controller.state();
  auto* snapshot = new ClientsSnapshot{state.clients, context.selected_client_id,
                                       state.pairing ? state.pairing->payload : "",
                                       gtk_widget_get_width(GTK_WIDGET(context.window)),
                                       context.content_width};
  g_object_set_data_full(G_OBJECT(page), snapshot_key, snapshot,
                         +[](gpointer data) { delete static_cast<ClientsSnapshot*>(data); });
}

void update_label(GtkWidget* label, const std::string& text)
{
  if (text != gtk_label_get_text(GTK_LABEL(label)))
    gtk_label_set_text(GTK_LABEL(label), text.c_str());
}

void refresh_connection(GtkWidget* label, bool connected)
{
  update_label(label, connected ? "● Online" : "○ Offline");
  gtk_widget_remove_css_class(label, connected ? "offline" : "online");
  gtk_widget_add_css_class(label, connected ? "online" : "offline");
}

bool refresh_live_clients(GtkWidget* page, LinuxPageContext context)
{
  const auto* snapshot =
      static_cast<ClientsSnapshot*>(g_object_get_data(G_OBJECT(page), snapshot_key));
  const auto& state = context.controller.state();
  if (!snapshot || snapshot->selection != context.selected_client_id ||
      snapshot->pairing_payload != (state.pairing ? state.pairing->payload : "") ||
      snapshot->window_width != gtk_widget_get_width(GTK_WIDGET(context.window)) ||
      snapshot->content_width != context.content_width ||
      !std::ranges::equal(snapshot->clients, state.clients,
                          [](const auto& previous, const auto& current)
                          { return client_structure(previous) == client_structure(current); }))
    return false;

  auto* roster = ui::find_widget(page, "clients-roster");
  for (const auto& client : state.clients)
  {
    auto* row = ui::find_widget(roster, "client-row-" + client.id);
    update_label(ui::find_widget(row, "client-facts"),
                  client.backend + " · " + client.role + " · " + client.last_seen);
    refresh_connection(ui::find_widget(row, "client-connection"), client.connected);
    auto* code = ui::find_widget(row, "client-identification-code");
    update_label(code, client.identification_code);
    gtk_widget_set_visible(code, !client.identification_code.empty());
    set_linux_client_filter_data(row, client);
  }
  auto selected = std::ranges::find(state.clients, context.selected_client_id,
                                    &ManagementClientRow::id);
  if (selected == state.clients.end())
    selected = state.clients.begin();
  if (selected != state.clients.end())
  {
    auto* details = ui::find_widget(page, "clients-inspector");
    refresh_connection(ui::find_widget(details, "client-connection"), selected->connected);
    update_label(ui::find_widget(details, "client-last-seen"), selected->last_seen);
    gtk_widget_set_sensitive(ui::find_widget(details, "client-identify"),
                              selected->connected && !state.identification);
  }
  gtk_widget_set_sensitive(ui::find_widget(page, "clients-identify-all"),
                            !state.identification &&
                                std::ranges::any_of(state.clients, &ManagementClientRow::connected));
  refresh_linux_clients_toolbar(ui::find_widget(page, "clients-toolbar"), context, roster);
  return true;
}

GtkWidget* client_action_button(GtkWidget* icon,
                                std::string_view accessible_name,
                                std::function<void()> action,
                                bool enabled = true,
                                std::string_view visible_label = {})
{
  auto* result = ui::button("", std::move(action), enabled);
  auto* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  ui::append(content, icon);
  if (!visible_label.empty())
    ui::append(content, gtk_label_new(std::string(visible_label).c_str()));
  gtk_button_set_child(GTK_BUTTON(result), content);
  gtk_widget_set_size_request(result, 36, 34);
  const std::string name(accessible_name);
  gtk_widget_set_tooltip_text(result, name.c_str());
  gtk_accessible_update_property(
      GTK_ACCESSIBLE(result), GTK_ACCESSIBLE_PROPERTY_LABEL, name.c_str(), -1);
  return result;
}

GtkWidget* wrapping_row()
{
  auto* row = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(row), GTK_SELECTION_NONE);
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(row), 1);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(row), 6);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(row), 8);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(row), 8);
  return row;
}

void insert(GtkWidget* flow, GtkWidget* child)
{
  gtk_flow_box_insert(GTK_FLOW_BOX(flow), child, -1);
}

GtkWidget* connection_status(const ManagementClientRow& client)
{
  auto* label = gtk_label_new(client.connected ? "● Online" : "○ Offline");
  gtk_widget_set_name(label, "client-connection");
  gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
  gtk_widget_add_css_class(label, "dispatch-connection");
  gtk_widget_add_css_class(label, client.connected ? "online" : "offline");
  return label;
}

void detail_fact(GtkWidget* grid, int row, std::string_view title, GtkWidget* value)
{
  auto* label = ui::description(title);
  gtk_widget_add_css_class(label, "dispatch-fact");
  gtk_widget_add_css_class(value, "dispatch-fact");
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), value, 1, row, 1, 1);
}

GtkWidget* role_action(LinuxPageContext context, const ManagementClientRow& client)
{
  return client_action_button(
      ui::dispatch_icon("clients", 15),
      client.can_set_viewer ? "Demote to viewer" : "Promote to operator",
      [context, id = client.id, demote = client.can_set_viewer]
      {
        if (demote)
          context.controller.set_viewer(id);
        else
          context.controller.set_operator(id);
        context.redraw();
      },
      client.can_set_operator || client.can_set_viewer,
      client.can_set_viewer ? "Set viewer" : "Set operator");
}

GtkWidget* forget_action(LinuxPageContext context, const ManagementClientRow& client)
{
  return client_action_button(
      ui::dispatch_icon("forget", 15),
      "Forget client",
      [context, client]
      {
        ui::confirm(context.window,
                    "Forget paired client",
                    "Forget '" + client.name +
                        "'? Its pairing credential will be deleted and the device will disconnect "
                        "immediately.",
                    [context, id = client.id]
                    {
                      context.controller.forget_client(id);
                      context.redraw();
                    });
      },
      true,
      "Forget");
}

GtkWidget* prompting_action(LinuxPageContext context, const ManagementClientRow& client)
{
  const bool attached = client.attached_script_count.value_or(0) > 0;
  auto* image = gtk_overlay_new();
  gtk_overlay_set_child(GTK_OVERLAY(image), ui::dispatch_icon("teleprompter", 15));
  if (attached)
  {
    auto* dot = gtk_label_new("●");
    gtk_widget_set_halign(dot, GTK_ALIGN_END);
    gtk_widget_set_valign(dot, GTK_ALIGN_END);
    gtk_overlay_add_overlay(GTK_OVERLAY(image), dot);
    gtk_widget_add_css_class(image, "attached-script-icon");
  }
  const auto detail = client.attached_script_count
                          ? std::to_string(*client.attached_script_count) + " attached scripts"
                          : "attachments unavailable";
  return ui::named(client_action_button(
                       image,
                       "Teleprompter - " + detail,
                       [context, id = client.id, name = client.name]
                       {
                         context.controller.teleprompter().open(id, name);
                         context.redraw();
                       },
                       true,
                       "Teleprompter"),
                   "client-teleprompter");
}

GtkWidget* inspector(LinuxPageContext context, const ManagementClientRow& client)
{
  auto* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_name(body, "clients-inspector");
  gtk_widget_add_css_class(body, "dispatch-inspector");
  gtk_widget_set_valign(body, GTK_ALIGN_START);
  auto* overline = ui::description("SELECTED CLIENT / " + client.id.substr(0, 8));
  gtk_widget_set_tooltip_text(overline, client.id.c_str());
  gtk_label_set_max_width_chars(GTK_LABEL(overline), 28);
  gtk_widget_add_css_class(overline, "dispatch-overline");
  ui::append(body, overline);
  auto* name = ui::section_title(client.name);
  gtk_label_set_max_width_chars(GTK_LABEL(name), 20);
  ui::append(body, name);
  auto* backend = ui::description(client.backend + " · " + client.role);
  gtk_label_set_max_width_chars(GTK_LABEL(backend), 28);
  ui::append(body, backend);
  auto* facts = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(facts), 20);
  gtk_grid_set_row_spacing(GTK_GRID(facts), 12);
  gtk_widget_set_margin_top(facts, 6);
  gtk_widget_set_margin_bottom(facts, 6);
  detail_fact(facts, 0, "Connection", connection_status(client));
  detail_fact(facts, 1, "Last seen",
              ui::named(ui::description(client.last_seen), "client-last-seen"));
  detail_fact(facts,
              2,
              "Attached scripts",
              ui::description(client.attached_script_count
                                  ? std::to_string(*client.attached_script_count)
                                  : "Unavailable"));
  ui::append(body, facts);
  auto* actions = wrapping_row();
  if (gtk_widget_get_width(GTK_WIDGET(context.window)) > 1100)
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(actions), 1);
  auto* identify = client_action_button(
      ui::dispatch_icon("identify", 15),
      "Identify client",
      [context, id = client.id]
      {
        context.controller.identify_client(id);
        context.redraw();
      },
      client.connected && !context.controller.state().identification,
      "Identify");
  insert(actions, ui::named(identify, "client-identify"));
  insert(actions, ui::named(role_action(context, client), "client-role"));
  insert(actions, prompting_action(context, client));
  insert(actions, ui::named(forget_action(context, client), "client-forget"));
  ui::append(body, actions);
  return body;
}

GtkWidget* client_record(LinuxPageContext context,
                         const ManagementClientRow& client,
                         std::size_t index,
                         bool selected)
{
  auto* button = ui::button("",
                            [context, id = client.id]
                            {
                              if (context.select_client)
                                context.select_client(id);
                            });
  gtk_widget_add_css_class(button, "dispatch-client");
  gtk_widget_set_name(button, ("client-row-" + client.id).c_str());
  if (selected)
    gtk_widget_add_css_class(button, "selected");
  gtk_widget_set_hexpand(button, TRUE);
  gtk_accessible_update_property(
      GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, client.name.c_str(), -1);
  gtk_accessible_update_state(GTK_ACCESSIBLE(button),
                              GTK_ACCESSIBLE_STATE_PRESSED,
                              selected ? GTK_ACCESSIBLE_TRISTATE_TRUE
                                       : GTK_ACCESSIBLE_TRISTATE_FALSE,
                              -1);
  auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  const auto ordinal = (index < 9 ? "0" : "") + std::to_string(index + 1);
  auto* number = gtk_label_new(ordinal.c_str());
  gtk_widget_add_css_class(number, "dispatch-number");
  gtk_widget_set_valign(number, GTK_ALIGN_START);
  ui::append(row, number);
  auto* identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_hexpand(identity, TRUE);
  auto* name = ui::section_title(client.name);
  gtk_label_set_wrap(GTK_LABEL(name), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(name), PANGO_WRAP_WORD_CHAR);
  ui::append(identity, name);
  const auto scripts = client.attached_script_count
                           ? std::to_string(*client.attached_script_count) + " scripts"
                           : "attachments unavailable";
  auto* code = ui::description(client.id.substr(0, 8) + " · " + scripts);
  gtk_widget_add_css_class(code, "dispatch-client-code");
  gtk_widget_set_tooltip_text(code, client.id.c_str());
  ui::append(identity, code);
  auto* facts = ui::description(client.backend + " · " + client.role + " · " + client.last_seen);
  gtk_widget_set_name(facts, "client-facts");
  gtk_widget_add_css_class(facts, "dispatch-client-facts");
  ui::append(identity, facts);
  auto* status = connection_status(client);
  gtk_widget_set_halign(status, GTK_ALIGN_START);
  auto* code_label =
      ui::named(ui::description(client.identification_code), "client-identification-code");
  gtk_widget_set_visible(code_label, !client.identification_code.empty());
  ui::append(identity, code_label);
  ui::append(row, identity);
  gtk_widget_set_hexpand(row, TRUE);
  auto* columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  ui::append(columns, row);
  gtk_widget_set_size_request(status, 72, -1);
  ui::append(columns, status);
  if (gtk_widget_get_width(GTK_WIDGET(context.window)) > 750)
  {
    auto* role = ui::description(client.role);
    gtk_widget_set_size_request(role, 76, -1);
    gtk_widget_add_css_class(role, "dispatch-fact");
    ui::append(columns, role);
  }
  gtk_button_set_child(GTK_BUTTON(button), columns);
  set_linux_client_filter_data(button, client);
  return button;
}

GtkWidget* roster_heading(LinuxPageContext context)
{
  auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_add_css_class(row, "dispatch-table-heading");
  auto* client = ui::description("Client");
  gtk_widget_set_hexpand(client, TRUE);
  ui::append(row, client);
  auto* status = ui::description("Status");
  gtk_widget_set_size_request(status, 72, -1);
  ui::append(row, status);
  if (gtk_widget_get_width(GTK_WIDGET(context.window)) > 750)
  {
    auto* role = ui::description("Role");
    gtk_widget_set_size_request(role, 76, -1);
    ui::append(row, role);
  }
  return row;
}

GtkWidget* page_commands(LinuxPageContext context)
{
  auto* commands = wrapping_row();
  const auto& state = context.controller.state();
  insert(commands,
         ui::named(client_action_button(
                       ui::dispatch_icon("identify", 15),
                       "Identify all",
                       [context]
                       {
                         context.controller.identify_all_clients();
                         context.redraw();
                       },
                       !state.identification &&
                           std::ranges::any_of(state.clients, &ManagementClientRow::connected),
                       "Identify all"),
                   "clients-identify-all"));
  insert(commands,
         ui::named(client_action_button(
                       ui::dispatch_icon("refresh", 15),
                       "Refresh",
                       [context]
                       {
                         context.controller.refresh_clients();
                         context.redraw();
                       },
                       true,
                       "Refresh"),
                   "clients-refresh"));
  auto* pair = client_action_button(
      ui::dispatch_icon("plus", 15),
      "Pair new client",
      [context]
      {
        context.controller.pair_client();
        context.redraw();
      },
      true,
      "Pair new client");
  gtk_widget_add_css_class(pair, "suggested-action");
  if (gtk_widget_get_width(GTK_WIDGET(context.window)) < 700)
  {
    auto* stacked = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    ui::append(stacked, ui::named(pair, "clients-pair"));
    ui::append(stacked, commands);
    return stacked;
  }
  insert(commands, ui::named(pair, "clients-pair"));
  return commands;
}

GtkWidget* pairing_panel(LinuxPageContext context, const ManagementPairing& pairing)
{
  auto* panel = wrapping_row();
  gtk_widget_set_name(panel, "pairing-card");
  auto* qr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  ui::append(qr, make_linux_pairing_qr(pairing.payload));
  ui::append(qr,
             ui::named(client_action_button(gtk_image_new_from_icon_name("zoom-in-symbolic"),
                                            "Enlarge pairing QR for 30 seconds",
                                            [context]
                                            {
                                              if (context.enlarge_pairing)
                                                context.enlarge_pairing();
                                            }),
                       "pairing-enlarge"));
  insert(panel, qr);
  auto* detail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  ui::append(detail, ui::section_title("Pair a new client"));
  ui::append(
      detail,
      ui::description("Scan this single-use code with the PaneLive mobile app. It is cleared "
                      "automatically when used or expired."));
  ui::append(detail,
             ui::description("This pairing code expires automatically and can be used only once."));
  insert(panel, detail);
  return panel;
}
} // namespace

GtkWidget* make_linux_clients_page(LinuxPageContext context)
{
  if (!context.controller.teleprompter().state().device_id.empty())
    return make_linux_teleprompter_page(context);
  auto* result = ui::page("Clients", "Pair devices and manage access to production backends.");
  gtk_widget_set_name(result, "clients-page");
  gtk_widget_add_css_class(result, "dispatch-clients");
  remember_clients(result, context);
  auto* commands = page_commands(context);
  gtk_widget_set_valign(commands, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class(commands, "dispatch-page-actions");
  ui::flow_append(gtk_widget_get_first_child(result), commands);
  const auto& state = context.controller.state();
  if (state.pairing)
    ui::append(result, pairing_panel(context, *state.pairing));
  if (state.clients.empty())
  {
    auto* roster = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(roster, "clients-roster");
    ui::append(roster, ui::description("No paired clients yet. Pair a device to begin."));
    ui::append(result, make_linux_clients_toolbar(context, roster));
    ui::append(result, roster);
    return result;
  }
  auto selected =
      std::ranges::find(state.clients, context.selected_client_id, &ManagementClientRow::id);
  if (selected == state.clients.end())
    selected = state.clients.begin();
  const bool wide = gtk_widget_get_width(GTK_WIDGET(context.window)) > 1100;
  auto* columns = wide ? gtk_grid_new() : gtk_box_new(GTK_ORIENTATION_VERTICAL, 30);
  gtk_widget_set_name(columns, "clients-columns");
  gtk_widget_add_css_class(columns, "dispatch-columns");
  auto* roster = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_name(roster, "clients-roster");
  gtk_widget_set_hexpand(roster, TRUE);
  gtk_widget_set_valign(roster, GTK_ALIGN_START);
  ui::append(roster, roster_heading(context));
  for (std::size_t index = 0; index < state.clients.size(); ++index)
    ui::append(roster,
               client_record(
                   context, state.clients[index], index, state.clients[index].id == selected->id));
  auto* details = inspector(context, *selected);
  gtk_widget_set_hexpand(details, TRUE);
  if (wide)
  {
    gtk_widget_add_css_class(details, "side-inspector");
    const auto available = std::max(0, context.content_width - 110);
    // The outer scroller does not propagate this width to the window, so resizing can
    // still cross the stacking breakpoint. The grid measures both columns at their widths.
    gtk_widget_set_hexpand(roster, FALSE);
    gtk_widget_set_size_request(roster, available * 165 / 265, -1);
    gtk_grid_set_column_spacing(GTK_GRID(columns), 30);
    gtk_grid_attach(GTK_GRID(columns), roster, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(columns), details, 1, 0, 1, 1);
  }
  else
  {
    ui::append(columns, roster);
    ui::append(columns, details);
  }
  ui::append(result, make_linux_clients_toolbar(context, roster));
  ui::append(result, columns);
  return result;
}

void refresh_linux_clients_page(GtkWidget* page, LinuxPageContext context)
{
  // Keep rows, action buttons and focus attached during routine observations, as on Windows.
  // Structural changes still rebuild callbacks and layout from the current snapshot.
  if (refresh_live_clients(page, context))
    return;
  const std::unique_ptr<GtkWidget, decltype(&g_object_unref)> replacement(
      GTK_WIDGET(g_object_ref_sink(make_linux_clients_page(context))), g_object_unref);
  auto* toolbar = ui::find_widget(page, "clients-toolbar");
  auto* replacement_toolbar = ui::find_widget(replacement.get(), "clients-toolbar");
  auto* roster = ui::find_widget(replacement.get(), "clients-roster");
  refresh_linux_clients_toolbar(toolbar, context, roster);
  for (auto* child = gtk_widget_get_first_child(page); child;)
  {
    auto* next = gtk_widget_get_next_sibling(child);
    if (child != toolbar)
      gtk_box_remove(GTK_BOX(page), child);
    child = next;
  }
  GtkWidget* previous = nullptr;
  for (auto* child = gtk_widget_get_first_child(replacement.get()); child;)
  {
    auto* next = gtk_widget_get_next_sibling(child);
    if (child == replacement_toolbar)
    {
      gtk_box_reorder_child_after(GTK_BOX(page), toolbar, previous);
      previous = toolbar;
    }
    else
    {
      const std::unique_ptr<GtkWidget, decltype(&g_object_unref)> retained(
          GTK_WIDGET(g_object_ref(child)), g_object_unref);
      gtk_box_remove(GTK_BOX(replacement.get()), child);
      gtk_box_append(GTK_BOX(page), child);
      previous = child;
    }
    child = next;
  }
  remember_clients(page, context);
}
} // namespace studio_remote::linux
