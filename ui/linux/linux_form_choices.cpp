#include "linux_ui_helpers.hpp"

#include <algorithm>
#include <memory>

namespace studio_remote::linux::ui
{
namespace
{
bool popup_open(GtkWidget* widget)
{
  if (GTK_IS_POPOVER(widget) && gtk_widget_get_visible(widget))
    return true;
  for (auto* child = gtk_widget_get_first_child(widget); child;
       child = gtk_widget_get_next_sibling(child))
    if (popup_open(child))
      return true;
  return false;
}

std::string choice_id(GObject* item)
{
  const auto* id = item ? static_cast<const char*>(g_object_get_data(item, "choice-id")) : nullptr;
  return id ? id : "";
}
} // namespace

std::string selected_choice(GtkWidget* field)
{
  return choice_id(G_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(field))));
}

void refresh_choices(GtkWidget* field,
                     std::vector<std::pair<std::string, std::string>> choices,
                     std::string_view initial_selection)
{
  if (popup_open(field))
    return;
  auto* dropdown = GTK_DROP_DOWN(field);
  auto selected = selected_choice(field);
  if (selected.empty())
    selected = initial_selection;
  if (!selected.empty() &&
      std::ranges::none_of(choices,
                           [&selected](const auto& choice) { return choice.first == selected; }))
  {
    auto* item = gtk_drop_down_get_selected_item(dropdown);
    choices.emplace_back(selected,
                         item ? gtk_string_object_get_string(GTK_STRING_OBJECT(item)) : selected);
  }
  auto* previous = gtk_drop_down_get_model(dropdown);
  bool unchanged = previous && g_list_model_get_n_items(previous) == choices.size();
  for (guint index = 0; unchanged && index < choices.size(); ++index)
  {
    const std::unique_ptr<GObject, decltype(&g_object_unref)> item(
        G_OBJECT(g_list_model_get_item(previous, index)), g_object_unref);
    unchanged =
        choice_id(item.get()) == choices[index].first &&
        gtk_string_object_get_string(GTK_STRING_OBJECT(item.get())) == choices[index].second;
  }
  if (unchanged)
    return;
  const std::unique_ptr<GtkStringList, decltype(&g_object_unref)> model(
      gtk_string_list_new(nullptr), g_object_unref);
  guint selection = GTK_INVALID_LIST_POSITION;
  for (guint index = 0; index < choices.size(); ++index)
  {
    const auto& [id, label] = choices[index];
    gtk_string_list_append(model.get(), label.c_str());
    const std::unique_ptr<GObject, decltype(&g_object_unref)> item(
        G_OBJECT(g_list_model_get_item(G_LIST_MODEL(model.get()), index)), g_object_unref);
    g_object_set_data_full(item.get(), "choice-id", g_strdup(id.c_str()), g_free);
    if (id == selected)
      selection = index;
  }
  gtk_drop_down_set_model(dropdown, G_LIST_MODEL(model.get()));
  gtk_drop_down_set_selected(dropdown, selection);
}
} // namespace studio_remote::linux::ui
