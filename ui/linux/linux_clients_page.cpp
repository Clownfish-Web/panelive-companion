#include "linux_management_pages.hpp"

#include "linux_pairing_qr.hpp"
#include "linux_ui_helpers.hpp"

#include <algorithm>
#include <array>

namespace studio_remote::linux
{
namespace
{
const char* device_icon_name(DeviceType type)
{
  switch (type)
  {
  case DeviceType::smartphone:
    return "network-cellular-symbolic";
  case DeviceType::tablet:
    return "input-tablet-symbolic";
  case DeviceType::unknown:
    return "computer-symbolic";
  }
  return "computer-symbolic";
}

GtkWidget* device_icon(DeviceType type, int pixel_size)
{
  auto* icon = gtk_image_new_from_icon_name(device_icon_name(type));
  gtk_image_set_pixel_size(GTK_IMAGE(icon), pixel_size);
  return icon;
}

GtkWidget* client_action_button(GtkWidget* icon,
                                std::string_view accessible_name,
                                std::function<void()> action,
                                bool enabled = true)
{
  auto* result = ui::button("", std::move(action), enabled);
  gtk_button_set_child(GTK_BUTTON(result), icon);
  gtk_widget_set_size_request(result, 36, 34);
  const std::string name(accessible_name);
  gtk_widget_set_tooltip_text(result, name.c_str());
  gtk_accessible_update_property(
      GTK_ACCESSIBLE(result), GTK_ACCESSIBLE_PROPERTY_LABEL, name.c_str(), -1);
  return result;
}

GtkWidget* operator_icon(DeviceType type)
{
  auto* result = gtk_overlay_new();
  gtk_widget_set_size_request(result, 22, 22);
  gtk_overlay_set_child(GTK_OVERLAY(result), device_icon(type, 17));
  auto* edit = gtk_image_new_from_icon_name("document-edit-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(edit), 10);
  gtk_widget_set_halign(edit, GTK_ALIGN_END);
  gtk_widget_set_valign(edit, GTK_ALIGN_END);
  gtk_overlay_add_overlay(GTK_OVERLAY(result), edit);
  return result;
}

GtkWidget* table_label(std::string_view value, int width, bool heading = false)
{
  auto* result = gtk_label_new(std::string(value).c_str());
  gtk_label_set_xalign(GTK_LABEL(result), 0.0F);
  gtk_label_set_wrap(GTK_LABEL(result), TRUE);
  gtk_widget_set_size_request(result, width, -1);
  if (heading)
    gtk_widget_add_css_class(result, "dim-label");
  return result;
}
} // namespace

GtkWidget* make_linux_clients_page(LinuxPageContext context)
{
  using namespace ui;
  if (!context.controller.teleprompter().state().device_id.empty())
    return make_linux_teleprompter_page(context);
  auto* result = page("Clients", "Pair devices and manage access to production backends.");
  auto* commands = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(commands, GTK_ALIGN_END);
  auto* identify_all = button(
      "Identify all",
      [context]
      {
        context.controller.identify_all_clients();
        context.redraw();
      },
      !context.controller.state().identification &&
          std::ranges::any_of(context.controller.state().clients, &ManagementClientRow::connected));
  gtk_widget_set_name(identify_all, "clients-identify-all");
  append(commands, identify_all);
  auto* refresh = button("Refresh",
                         [context]
                         {
                           context.controller.refresh_clients();
                           context.redraw();
                         });
  gtk_widget_set_name(refresh, "clients-refresh");
  append(commands, refresh);
  auto* pair = button("Pair new client",
                      [context]
                      {
                        context.controller.pair_client();
                        context.redraw();
                      });
  gtk_widget_set_name(pair, "clients-pair");
  gtk_widget_add_css_class(pair, "suggested-action");
  append(commands, pair);
  append(result, commands);

  const auto& state = context.controller.state();
  if (state.pairing)
  {
    auto* pairing = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_name(pairing, "pairing-card");
    append(pairing, make_linux_pairing_qr(state.pairing->payload));
    auto* enlarge = client_action_button(gtk_image_new_from_icon_name("zoom-in-symbolic"),
                                         "Enlarge pairing QR for 30 seconds",
                                         [context]
                                         {
                                           if (context.enlarge_pairing)
                                             context.enlarge_pairing();
                                         });
    gtk_widget_set_name(enlarge, "pairing-enlarge");
    gtk_widget_set_valign(enlarge, GTK_ALIGN_CENTER);
    append(pairing, enlarge);
    auto* explanation = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_valign(explanation, GTK_ALIGN_CENTER);
    append(explanation, section_title("Pair a new client"));
    append(explanation,
           description("Scan this single-use code with the PaneLive mobile app. It is cleared "
                       "automatically when used or expired."));
    append(explanation,
           badge("This pairing code expires automatically and can be used only once.", "warning"));
    append(pairing, explanation);
    append(result, card(pairing));
  }

  auto* clients = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
  auto* headings = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(headings), 12);
  gtk_widget_set_margin_top(headings, 8);
  gtk_widget_set_margin_bottom(headings, 8);
  gtk_widget_set_margin_start(headings, 14);
  gtk_widget_set_margin_end(headings, 14);
  const std::array heading_values{std::pair{"Status", 80},
                                  std::pair{"Client", 210},
                                  std::pair{"Backend", 100},
                                  std::pair{"Role", 80},
                                  std::pair{"Last seen", 130},
                                  std::pair{"Actions", 210}};
  for (std::size_t column = 0; column < heading_values.size(); ++column)
  {
    GtkWidget* heading = nullptr;
    if (column < 5)
    {
      const auto sort_column = static_cast<ManagementClientColumn>(column);
      const auto sort = state.client_sort;
      const auto indicator =
          sort.column == sort_column
              ? (sort.direction == ManagementSortDirection::ascending ? " ↑" : " ↓")
              : " ↕";
      heading = button(std::string(heading_values[column].first) + indicator,
                       [context, sort_column]
                       {
                         context.controller.sort_clients(sort_column);
                         context.redraw();
                       });
      gtk_widget_add_css_class(heading, "flat");
      gtk_widget_set_size_request(heading, heading_values[column].second, -1);
      gtk_widget_set_tooltip_text(heading, "Sort column; click again to reverse");
    }
    else
      heading = table_label(heading_values[column].first, heading_values[column].second, true);
    gtk_grid_attach(GTK_GRID(headings), heading, static_cast<int>(column), 0, 1, 1);
  }
  gtk_widget_set_hexpand(gtk_grid_get_child_at(GTK_GRID(headings), 1, 0), TRUE);
  append(clients, headings);
  append(clients, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  for (const auto& client : state.clients)
  {
    auto* client_row = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(client_row), 12);
    gtk_widget_set_margin_top(client_row, 8);
    gtk_widget_set_margin_bottom(client_row, 8);
    gtk_widget_set_margin_start(client_row, 14);
    gtk_widget_set_margin_end(client_row, 14);
    auto* identity = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(identity, TRUE);
    gtk_widget_set_size_request(identity, 210, -1);
    append(identity, device_icon(client.device_type, 18));
    auto* name = gtk_label_new(client.name.c_str());
    gtk_label_set_xalign(GTK_LABEL(name), 0.0F);
    gtk_label_set_wrap(GTK_LABEL(name), TRUE);
    gtk_widget_add_css_class(name, "heading");
    append(identity, name);
    if (!client.identification_code.empty())
    {
      auto* code = gtk_label_new(client.identification_code.c_str());
      gtk_widget_set_name(code, "client-identification-code");
      gtk_widget_set_tooltip_text(code, "Identification code");
      append(identity, code);
    }
    auto* status =
        badge(client.connected ? "Online" : "Offline", client.connected ? "healthy" : "neutral");
    gtk_widget_set_size_request(status, 80, -1);
    gtk_widget_set_halign(status, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(client_row), status, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(client_row), identity, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(client_row), table_label(client.backend, 100), 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(client_row), table_label(client.role, 80), 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(client_row), table_label(client.last_seen, 130), 4, 0, 1, 1);
    auto* change_role = client_action_button(
        client.can_set_viewer ? device_icon(client.device_type, 17)
                              : operator_icon(client.device_type),
        client.can_set_viewer ? "Demote to viewer" : "Promote to operator",
        [context, id = client.id, demote = client.can_set_viewer]
        {
          if (demote)
            context.controller.set_viewer(id);
          else
            context.controller.set_operator(id);
          context.redraw();
        },
        client.can_set_operator || client.can_set_viewer);
    auto* forget = client_action_button(
        gtk_image_new_from_icon_name("edit-delete-symbolic"),
        "Forget client",
        [context, client]
        {
          confirm(context.window,
                  "Forget paired client",
                  "Forget '" + client.name +
                      "'? Its pairing credential will be deleted and the device will disconnect "
                      "immediately.",
                  [context, id = client.id]
                  {
                    context.controller.forget_client(id);
                    context.redraw();
                  });
        });
    auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_size_request(actions, 210, -1);
    auto* identify = client_action_button(
        gtk_image_new_from_icon_name("find-location-symbolic"),
        "Identify client",
        [context, id = client.id]
        {
          context.controller.identify_client(id);
          context.redraw();
        },
        client.connected && !state.identification);
    gtk_widget_set_name(identify, "client-identify");
    append(actions, identify);
    append(actions, change_role);
    const bool attached = client.attached_script_count.value_or(0) > 0;
    auto* prompting_icon = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(prompting_icon),
                          gtk_image_new_from_icon_name("text-x-generic-symbolic"));
    if (attached)
    {
      auto* dot = gtk_label_new("●");
      gtk_widget_set_halign(dot, GTK_ALIGN_END);
      gtk_widget_set_valign(dot, GTK_ALIGN_END);
      gtk_overlay_add_overlay(GTK_OVERLAY(prompting_icon), dot);
      gtk_widget_add_css_class(prompting_icon, "attached-script-icon");
    }
    const auto detail = client.attached_script_count
                            ? std::to_string(*client.attached_script_count) + " attached scripts"
                            : "attachments unavailable";
    auto* prompting = client_action_button(prompting_icon,
                                           "Teleprompter - " + detail,
                                           [context, id = client.id, name = client.name]
                                           {
                                             context.controller.teleprompter().open(id, name);
                                             context.redraw();
                                           });
    gtk_widget_set_name(prompting, "client-teleprompter");
    append(actions, prompting);
    append(actions, forget);
    gtk_grid_attach(GTK_GRID(client_row), actions, 5, 0, 1, 1);
    append(clients, client_row);
  }
  if (state.clients.empty())
    append(clients, description("No paired clients yet. Pair a device to begin."));
  append(result, card(clients));
  return result;
}
} // namespace studio_remote::linux
