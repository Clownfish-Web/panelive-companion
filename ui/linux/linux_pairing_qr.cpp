#include "linux_pairing_qr.hpp"

#include <memory>

namespace studio_remote::linux
{
GtkWidget* make_linux_pairing_image(const PairingQrImage& image, int display_size)
{
  const auto scaled = resample_pairing_qr_image(image, display_size);
  const std::unique_ptr<GBytes, decltype(&g_bytes_unref)> bytes(
      g_bytes_new(scaled.pixels.data(), scaled.pixels.size()), g_bytes_unref);
  const std::unique_ptr<GdkTexture, decltype(&g_object_unref)> texture(
      gdk_memory_texture_new(display_size,
                             display_size,
                             GDK_MEMORY_R8G8B8A8,
                             bytes.get(),
                             static_cast<gsize>(display_size) * 4),
      g_object_unref);
  auto* result = gtk_image_new_from_paintable(GDK_PAINTABLE(texture.get()));
  gtk_image_set_pixel_size(GTK_IMAGE(result), display_size);
  gtk_widget_set_size_request(result, display_size, display_size);
  return result;
}

GtkWidget* make_linux_pairing_qr(std::string_view payload, int display_size)
{
  return make_linux_pairing_image(make_pairing_qr_image(payload), display_size);
}
} // namespace studio_remote::linux
