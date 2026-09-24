#pragma once

#include "../shared/pairing_qr.hpp"

#include <gtk/gtk.h>

#include <string_view>

namespace studio_remote::linux
{
GtkWidget* make_linux_pairing_qr(std::string_view payload, int display_size = 180);
GtkWidget* make_linux_pairing_image(const PairingQrImage& image, int display_size);
} // namespace studio_remote::linux
