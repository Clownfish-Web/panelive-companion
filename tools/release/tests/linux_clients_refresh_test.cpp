#include "linux_management_pages.hpp"
#include "linux_ui_helpers.hpp"

#include <cassert>
#include <iostream>

using namespace studio_remote;
using namespace studio_remote::linux;

int main()
{
  gtk_init();
  std::vector<ManagementClientRow> clients(1);
  clients[0].id = "client-a";
  clients[0].name = "Tablet";
  clients[0].backend = "OBS";
  clients[0].role = "Viewer";
  clients[0].last_seen = "Just now";
  clients[0].connected = true;
  clients[0].can_set_operator = true;
  std::string promoted;
  ManagementUiCallbacks callbacks;
  callbacks.clients = [&] { return clients; };
  callbacks.set_operator = [&](std::string_view id) { promoted = id; };
  callbacks.identify_client = [](std::string_view, ClientIdentificationOptions) {};
  callbacks.unix_seconds = [] { return 100U; };
  ManagementUiController controller(ManagementInterfaceMode::advanced, ManagementPage::clients,
                                    {}, std::move(callbacks));
  controller.refresh();
  auto* window = GTK_WINDOW(gtk_window_new());
  auto* scroll = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
  gtk_window_set_child(window, GTK_WIDGET(scroll));
  LinuxPageContext context{controller, window, [] {}};
  context.selected_client_id = "client-a";
  context.content_width = 900;
  auto* page = make_linux_clients_page(context);
  gtk_scrolled_window_set_child(scroll, page);
  gtk_window_set_default_size(window, 1040, 720);
  gtk_window_present(window);
  for (int i = 0; i < 20; ++i)
  {
    while (g_main_context_iteration(nullptr, FALSE)) {}
    g_usleep(10000);
  }
  refresh_linux_clients_page(page, context);
  auto find = [&](const char* name) { return ui::find_widget(page, name); };
  auto* row = find("client-row-client-a");
  auto* identify = find("client-identify");
  auto* role = find("client-role");
  auto* search = find("clients-search");
  gtk_editable_set_text(GTK_EDITABLE(search), "Tab");
  gtk_widget_grab_focus(search);
  gtk_editable_select_region(GTK_EDITABLE(search), 1, 3);
  auto* focus = gtk_window_get_focus(window);
  auto refresh = [&]
  {
    controller.refresh();
    refresh_linux_clients_page(page, context);
  };
  for (int i = 0; i < 5; ++i)
  {
    clients[0].last_seen = std::to_string(i) + " seconds ago";
    clients[0].connected = i % 2 == 0;
    clients[0].identification_code = i % 2 == 0 ? "42" : "";
    refresh();
    assert(find("client-row-client-a") == row);
    assert(find("client-identify") == identify);
    assert(find("client-role") == role);
    assert(find("clients-search") == search);
    assert(gtk_window_get_focus(window) == focus);
    assert(gtk_widget_get_sensitive(identify) == clients[0].connected);
    auto* code = ui::find_widget(row, "client-identification-code");
    assert(gtk_widget_get_visible(code) == !clients[0].identification_code.empty());
    assert(clients[0].last_seen == gtk_label_get_text(GTK_LABEL(find("client-last-seen"))));
    int begin = 0, end = 0;
    assert(gtk_editable_get_selection_bounds(GTK_EDITABLE(search), &begin, &end));
    assert(begin == 1 && end == 3);
  }
  controller.identify_client("client-a");
  refresh_linux_clients_page(page, context);
  assert(!gtk_widget_get_sensitive(identify));
  assert(!gtk_widget_get_sensitive(find("clients-identify-all")));
  controller.cancel_client_identification();
  refresh_linux_clients_page(page, context);
  assert(gtk_widget_get_sensitive(identify));

  // Permission changes must replace captured actions instead of retaining stale gating.
  g_object_ref(role);
  clients[0].can_set_operator = false;
  refresh();
  assert(find("client-role") != role);
  assert(!gtk_widget_get_sensitive(find("client-role")));
  g_object_unref(role);
  clients[0].can_set_operator = true;
  refresh();
  g_signal_emit_by_name(find("client-role"), "clicked");
  assert(promoted == "client-a");
  assert(find("clients-search") == search);

  controller.show_pairing({"test-pairing", 200});
  refresh_linux_clients_page(page, context);
  assert(find("pairing-card"));
  controller.clear_pairing();
  refresh_linux_clients_page(page, context);
  assert(!find("pairing-card"));
  clients.clear();
  refresh();
  assert(!find("client-row-client-a"));
  assert(!gtk_widget_get_sensitive(find("clients-identify-all")));
  refresh();
  assert(find("clients-search") == search);
  gtk_window_destroy(window);
  std::cout << "PASS: retained rows/actions/search, live status, identification gating, "
               "permission changes, pairing removal and empty roster\n";
}
