#include "linux_management_pages.hpp"

#include "linux_ui_helpers.hpp"

namespace studio_remote::linux
{
namespace
{
GtkWidget* vertical_box()
{
  return gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
}

void style_apply_button(GtkWidget* button)
{
  gtk_widget_add_css_class(button, "suggested-action");
  gtk_widget_set_halign(button, GTK_ALIGN_END);
}
} // namespace

GtkWidget* make_linux_adapters_page(LinuxPageContext context)
{
  using namespace ui;
  auto* result = page("Adapters", "Configure adapter connections.");
  const auto& state = context.controller.state();

  auto* teleprompter = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
  append(teleprompter, section_title("PaneLive Teleprompter"));
  append(teleprompter, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  append(teleprompter,
         description("This built-in adapter starts automatically. Pair devices to share scripts "
                     "and playback. Use Status to start, stop or restart it. No production "
                     "software or connection settings are needed."));
  auto* teleprompter_card = card(teleprompter);
  gtk_widget_set_name(teleprompter_card, "teleprompter-adapter-card");
  append(result, teleprompter_card);

  auto* obs = vertical_box();
  auto* obs_heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  auto* obs_title = section_title("OBS Studio");
  gtk_widget_set_hexpand(obs_title, TRUE);
  append(obs_heading, obs_title);
  append(obs_heading,
         badge(state.obs_connection.status.empty() ? "Not configured" : state.obs_connection.status,
               state.obs_connection.connected ? "healthy" : "warning"));
  append(obs, obs_heading);
  auto* obs_host = entry(state.obs_connection.host);
  auto* obs_port = spin(state.obs_connection.port, 1, 65'535);
  auto* obs_password = entry({}, "Leave empty to keep the stored password", true);
  gtk_widget_set_name(obs_host, "obs-host");
  gtk_widget_set_name(obs_port, "obs-port");
  gtk_widget_set_name(obs_password, "obs-password");
  append(obs, form_row("Host", obs_host));
  append(obs, form_row("WebSocket port", obs_port));
  append(obs, form_row("Password", obs_password));
  auto* apply_obs = button("Apply OBS settings",
                           [context, obs_host, obs_port, obs_password]
                           {
                             ManagementObsConnectionRequest request;
                             request.host = text(obs_host);
                             request.port = port(obs_port);
                             if (const auto password = text(obs_password); !password.empty())
                               request.password = password;
                             context.controller.apply_obs_connection(std::move(request));
                             context.redraw();
                           });
  gtk_widget_set_name(apply_obs, "apply-obs");
  style_apply_button(apply_obs);
  append(obs, apply_obs);
  append(result, card(obs));

  return result;
}

GtkWidget* make_linux_companion_settings_page(LinuxPageContext context)
{
  using namespace ui;
  auto* result =
      page("Companion Settings", "Configure the HTTPS/WSS listener and pairing address.");
  const auto& network = context.controller.state().network_settings;
  auto* contents = vertical_box();
  auto* heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  auto* title = section_title("Companion network");
  gtk_widget_set_hexpand(title, TRUE);
  append(heading, title);
  append(heading,
         badge(network.listening ? "Listening" : "Unavailable",
               network.listening ? "healthy" : "error"));
  append(contents, heading);
  if (!network.warning.empty())
    append(contents, badge(network.warning, "warning"));

  auto* addresses = gtk_string_list_new(nullptr);
  guint selected = GTK_INVALID_LIST_POSITION;
  for (std::size_t index = 0; index < network.available_addresses.size(); ++index)
  {
    gtk_string_list_append(addresses, network.available_addresses[index].c_str());
    if (network.available_addresses[index] == network.address)
      selected = static_cast<guint>(index);
  }
  auto* address = gtk_drop_down_new(G_LIST_MODEL(addresses), nullptr);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(address), selected);
  auto* port_field = spin(network.port, 1, 65'535);
  gtk_widget_set_name(address, "network-address");
  gtk_widget_set_name(port_field, "network-port");
  append(contents, form_row("Pairing address", address));
  append(contents, form_row("HTTPS/WSS port", port_field));
  append(contents,
         description("The selected private LAN address is advertised for pairing. Changing the "
                     "port restarts the HTTPS/WSS listener in place and invalidates any open "
                     "pairing code."));
  auto* apply = button("Apply network settings",
                       [context, address, port_field]
                       {
                         ManagementNetworkSettingsRequest request;
                         if (auto* item = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(address)))
                         {
                           request.address = gtk_string_object_get_string(GTK_STRING_OBJECT(item));
                         }
                         request.port = port(port_field);
                         context.controller.apply_network_settings(std::move(request));
                         context.redraw();
                       });
  gtk_widget_set_name(apply, "apply-network");
  style_apply_button(apply);
  append(contents, apply);
  append(result, card(contents));
  return result;
}
} // namespace studio_remote::linux
