#pragma once

#include <gtk/gtk.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace studio_remote::linux::ui
{
struct FocusSnapshot
{
  std::string widget_name;
  std::optional<std::pair<int, int>> selection;
};

FocusSnapshot capture_focus(GtkWindow* window, GtkWidget* content);
void restore_focus(GtkWindow* window, GtkWidget* content, const FocusSnapshot& snapshot);

GtkWidget* page(std::string_view title, std::string_view description);
GtkWidget* dispatch_icon(std::string_view name, int size = 18);
GtkWidget* card(GtkWidget* contents);
GtkWidget* section_title(std::string_view text);
GtkWidget* description(std::string_view text);
GtkWidget* form_row(std::string_view title, GtkWidget* control, std::string_view detail = {});
// A GTK-owned wrapping group for parallel controls. Append children with flow_append.
GtkWidget* flow_row(unsigned int maximum_columns = 6);
void flow_append(GtkWidget* row, GtkWidget* child);
GtkWidget* entry(std::string_view value, std::string_view placeholder = {}, bool secret = false);
GtkWidget* spin(double value, double minimum, double maximum);
GtkWidget* button(std::string_view label, std::function<void()> action, bool enabled = true);
GtkWidget* external_link(const char* uri);
GtkWidget* toggle(bool active, std::function<void(bool)> action);
GtkWidget* badge(std::string_view text, std::string_view tone = "neutral");
GtkWidget* named(GtkWidget* widget, std::string_view name);
GtkWidget* find_widget(GtkWidget* parent, std::string_view name);
void refresh_badge(GtkWidget* widget, std::string_view text, std::string_view tone);
void refresh_toggle(GtkWidget* widget, bool active);
std::string selected_choice(GtkWidget* field);
void refresh_choices(GtkWidget* field,
                     std::vector<std::pair<std::string, std::string>> choices,
                     std::string_view initial_selection = {});
void append(GtkWidget* box, GtkWidget* child);
std::string text(GtkWidget* entry);
std::uint16_t port(GtkWidget* spin);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
void confirm(GtkWindow* parent,
             std::string_view title,
             std::string_view message,
             std::function<void()> confirmed,
             std::function<void()> cancelled = {});
G_GNUC_END_IGNORE_DEPRECATIONS
} // namespace studio_remote::linux::ui
