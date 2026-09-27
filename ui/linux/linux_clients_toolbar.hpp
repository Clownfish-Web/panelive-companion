#pragma once

#include "linux_management_pages.hpp"

namespace studio_remote::linux
{
// GTK-thread-only filtering of the displayed roster; never changes controller membership.
void set_linux_client_filter_data(GtkWidget* row, const ManagementClientRow& client);
GtkWidget* make_linux_clients_toolbar(LinuxPageContext context, GtkWidget* roster);
// Rebind before the previous roster is destroyed. The toolbar stays attached during refresh.
void refresh_linux_clients_toolbar(GtkWidget* toolbar, LinuxPageContext context, GtkWidget* roster);
} // namespace studio_remote::linux
