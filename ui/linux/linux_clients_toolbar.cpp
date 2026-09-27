#include "linux_clients_toolbar.hpp"

#include "linux_ui_helpers.hpp"

#include <array>
#include <exception>
#include <memory>
#include <utility>

namespace studio_remote::linux
{
namespace
{
constexpr auto filter_data_key = "panelive-client-filter";
constexpr auto search_binding_key = "panelive-client-search-binding";

struct ClientFilterData
{
  std::string searchable;
  bool online;
};

struct SearchBinding
{
  LinuxPageContext context;
  // Borrowed siblings, owned by the same page. No asynchronous work retains this binding.
  GtkWidget* roster;
  GtkWidget* count;
};

std::string casefold(std::string_view text)
{
  const std::unique_ptr<char, decltype(&g_free)> folded(
      g_utf8_casefold(text.data(), static_cast<gssize>(text.size())), g_free);
  return folded ? folded.get() : "";
}

void apply_filter(const SearchBinding& binding, std::string_view query)
{
  const auto folded = casefold(query);
  std::size_t visible = 0;
  std::size_t online = 0;
  for (auto* row = gtk_widget_get_first_child(binding.roster); row;
       row = gtk_widget_get_next_sibling(row))
  {
    const auto* data =
        static_cast<ClientFilterData*>(g_object_get_data(G_OBJECT(row), filter_data_key));
    if (!data)
      continue;
    const bool matches = data->searchable.find(folded) != std::string::npos;
    gtk_widget_set_visible(row, matches);
    if (matches)
    {
      ++visible;
      online += data->online ? 1U : 0U;
    }
  }
  const auto summary = std::to_string(visible) + (visible == 1 ? " client / " : " clients / ") +
                       std::to_string(online) + " online";
  gtk_label_set_text(GTK_LABEL(binding.count), summary.c_str());
}

void search_changed(GtkEditable* editable, gpointer data) noexcept
{
  try
  {
    const auto& binding = *static_cast<SearchBinding*>(data);
    const std::string query = gtk_editable_get_text(editable);
    if (binding.context.set_client_query)
      binding.context.set_client_query(query);
    apply_filter(binding, query);
  }
  catch (const std::exception& error)
  {
    g_warning("Client search failed: %s", error.what());
  }
  catch (...)
  {
    g_warning("Client search failed");
  }
}

void refresh_sort_label(GtkWidget* menu, LinuxPageContext context)
{
  constexpr std::array labels{"status", "name", "backend", "role", "last seen"};
  const auto sort = context.controller.state().client_sort;
  const auto label = std::string("Sort by ") + labels.at(static_cast<std::size_t>(sort.column)) +
                     (sort.direction == ManagementSortDirection::ascending ? " ↑" : " ↓");
  gtk_menu_button_set_label(GTK_MENU_BUTTON(menu), label.c_str());
}

GtkWidget* sort_menu(LinuxPageContext context)
{
  constexpr std::array labels{"status", "name", "backend", "role", "last seen"};
  auto* menu = gtk_menu_button_new();
  refresh_sort_label(menu, context);
  gtk_widget_set_name(menu, "clients-sort");
  auto* popover = gtk_popover_new();
  auto* choices = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  for (std::size_t index = 0; index < labels.size(); ++index)
  {
    auto* choice =
        ui::button(std::string("Sort by ") + labels[index],
                   [context, index, popover]
                   {
                     gtk_popover_popdown(GTK_POPOVER(popover));
                     context.controller.sort_clients(static_cast<ManagementClientColumn>(index));
                     context.redraw();
                   });
    ui::append(choices, choice);
  }
  gtk_popover_set_child(GTK_POPOVER(popover), choices);
  gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu), popover);
  return menu;
}
} // namespace

void set_linux_client_filter_data(GtkWidget* row, const ManagementClientRow& client)
{
  auto data = std::make_unique<ClientFilterData>(ClientFilterData{
      casefold(client.name + " " + client.backend + " " + client.role), client.connected});
  g_object_set_data_full(
      G_OBJECT(row),
      filter_data_key,
      data.release(),
      +[](gpointer value) { delete static_cast<ClientFilterData*>(value); });
}

GtkWidget* make_linux_clients_toolbar(LinuxPageContext context, GtkWidget* roster)
{
  const auto orientation =
      context.content_width > 950 ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
  auto* toolbar = gtk_box_new(orientation, 12);
  gtk_widget_set_name(toolbar, "clients-toolbar");
  gtk_widget_add_css_class(toolbar, "dispatch-toolbar");
  auto* search_group = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  auto* label = ui::description("Find client");
  gtk_label_set_wrap(GTK_LABEL(label), FALSE);
  ui::append(search_group, label);
  auto* search = gtk_entry_new();
  gtk_widget_set_name(search, "clients-search");
  gtk_widget_add_css_class(search, "dispatch-search");
  gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Name, backend or role");
  gtk_editable_set_width_chars(GTK_EDITABLE(search), 12);
  gtk_editable_set_max_width_chars(GTK_EDITABLE(search), 20);
  gtk_widget_set_hexpand(search, TRUE);
  gtk_editable_set_text(GTK_EDITABLE(search), context.client_query.c_str());
  gtk_accessible_update_property(
      GTK_ACCESSIBLE(search), GTK_ACCESSIBLE_PROPERTY_LABEL, "Find client", -1);
  ui::append(search_group, search);
  ui::append(toolbar, search_group);
  auto* sort = sort_menu(context);
  gtk_widget_set_halign(sort, GTK_ALIGN_START);
  ui::append(toolbar, sort);
  auto* count = ui::description("");
  gtk_label_set_wrap(GTK_LABEL(count), FALSE);
  gtk_widget_set_halign(count, GTK_ALIGN_END);
  gtk_widget_set_hexpand(count, TRUE);
  ui::append(toolbar, count);
  auto binding = std::make_unique<SearchBinding>(SearchBinding{context, roster, count});
  apply_filter(*binding, context.client_query);
  // Non-owning lookup; the search entry's signal closure owns the binding for this toolbar.
  g_object_set_data(G_OBJECT(toolbar), search_binding_key, binding.get());
  g_signal_connect_data(
      search,
      "changed",
      G_CALLBACK(search_changed),
      binding.release(),
      +[](gpointer data, GClosure*) { delete static_cast<SearchBinding*>(data); },
      static_cast<GConnectFlags>(0));
  return toolbar;
}

void refresh_linux_clients_toolbar(GtkWidget* toolbar, LinuxPageContext context, GtkWidget* roster)
{
  auto& binding =
      *static_cast<SearchBinding*>(g_object_get_data(G_OBJECT(toolbar), search_binding_key));
  binding.roster = roster;
  gtk_orientable_set_orientation(GTK_ORIENTABLE(toolbar),
                                 context.content_width > 950 ? GTK_ORIENTATION_HORIZONTAL
                                                             : GTK_ORIENTATION_VERTICAL);
  refresh_sort_label(ui::find_widget(toolbar, "clients-sort"), context);
  // Read the retained edit itself: never reset selection or active IME composition.
  apply_filter(binding, ui::text(ui::find_widget(toolbar, "clients-search")));
}
} // namespace studio_remote::linux
