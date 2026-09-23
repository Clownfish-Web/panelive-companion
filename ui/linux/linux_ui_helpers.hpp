#pragma once

#include <gtk/gtk.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace studio_remote::linux::ui
{
GtkWidget* page(std::string_view title, std::string_view description);
GtkWidget* card(GtkWidget* contents);
GtkWidget* section_title(std::string_view text);
GtkWidget* description(std::string_view text);
GtkWidget* form_row(std::string_view title, GtkWidget* control, std::string_view detail = {});
GtkWidget* entry(std::string_view value, std::string_view placeholder = {}, bool secret = false);
GtkWidget* spin(double value, double minimum, double maximum);
GtkWidget* button(std::string_view label, std::function<void()> action, bool enabled = true);
GtkWidget* toggle(bool active, std::function<void(bool)> action);
GtkWidget* badge(std::string_view text, std::string_view tone = "neutral");
void append(GtkWidget* box, GtkWidget* child);
std::string text(GtkWidget* entry);
std::uint16_t port(GtkWidget* spin);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
void confirm(GtkWindow* parent,
             std::string_view title,
             std::string_view message,
             std::function<void()> confirmed,
             std::function<void()> cancelled = {});
void approve(GtkWindow* parent,
             std::string_view title,
             std::string_view message,
             std::function<void(bool)> decision);
G_GNUC_END_IGNORE_DEPRECATIONS
} // namespace studio_remote::linux::ui
