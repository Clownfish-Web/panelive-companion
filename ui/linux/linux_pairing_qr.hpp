#pragma once

#include <gtk/gtk.h>

#include <string_view>

namespace studio_remote::linux
{
GtkWidget* make_linux_pairing_qr(std::string_view payload, int display_size = 180);
} // namespace studio_remote::linux
