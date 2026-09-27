#pragma once

#include <gtk/gtk.h>
#include <studio_remote/ui/management_controller.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace studio_remote::linux
{
struct LinuxPageContext
{
  ManagementUiController& controller;
  GtkWindow* window;
  std::function<void()> redraw;
  std::function<void()> enlarge_pairing = {};
  std::string selected_client_id = {};
  std::function<void(std::string)> select_client = {};
  int content_width = 0;
  std::string client_query = {};
  std::function<void(std::string)> set_client_query = {};
};

GtkWidget* make_linux_teleprompter_page(LinuxPageContext context);
GtkWidget* make_linux_teleprompter_editor(LinuxPageContext context);
GtkWidget* make_linux_teleprompter_sharing(LinuxPageContext context);
GtkWidget* make_linux_teleprompter_sharing_actions(LinuxPageContext context);
void refresh_linux_teleprompter_page(GtkWidget* page, LinuxPageContext context);
GtkWidget* make_linux_clients_page(LinuxPageContext context);
void refresh_linux_clients_page(GtkWidget* page, LinuxPageContext context);
GtkWidget* make_linux_adapters_page(LinuxPageContext context);
GtkWidget* make_linux_companion_settings_page(LinuxPageContext context);
GtkWidget* make_linux_status_page(LinuxPageContext context);
GtkWidget* make_linux_diagnostics_page(LinuxPageContext context);
GtkWidget* make_linux_advanced_settings_page(LinuxPageContext context);
GtkWidget* make_linux_about_page(std::string_view version);
// Update observations and immediate settings only, never reload uncommitted form fields.
void refresh_linux_adapters_page(GtkWidget* page, LinuxPageContext context);
void refresh_linux_companion_settings_page(GtkWidget* page, LinuxPageContext context);
void refresh_linux_advanced_settings_page(GtkWidget* page, LinuxPageContext context);
void refresh_linux_diagnostics_page(GtkWidget* page, LinuxPageContext context);
} // namespace studio_remote::linux
