#pragma once

#include <gtk/gtk.h>
#include <studio_remote/ui/management_controller.hpp>

#include <functional>
#include <string_view>

namespace studio_remote::linux
{
struct LinuxPageContext
{
  ManagementUiController& controller;
  GtkWindow* window;
  std::function<void()> redraw;
};

GtkWidget* make_linux_teleprompter_page(LinuxPageContext context);
GtkWidget* make_linux_teleprompter_editor(LinuxPageContext context);
GtkWidget* make_linux_teleprompter_sharing(LinuxPageContext context);
GtkWidget* make_linux_teleprompter_sharing_actions(LinuxPageContext context);
void refresh_linux_teleprompter_page(GtkWidget* page, LinuxPageContext context);
GtkWidget* make_linux_clients_page(LinuxPageContext context);
GtkWidget* make_linux_adapters_page(LinuxPageContext context);
GtkWidget* make_linux_companion_settings_page(LinuxPageContext context);
GtkWidget* make_linux_status_page(LinuxPageContext context);
GtkWidget* make_linux_diagnostics_page(LinuxPageContext context);
GtkWidget* make_linux_advanced_settings_page(LinuxPageContext context);
GtkWidget* make_linux_about_page(std::string_view version);
} // namespace studio_remote::linux
