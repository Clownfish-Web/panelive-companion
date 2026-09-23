#include "linux_pairing_qr.hpp"

#include <qrencode.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace studio_remote::linux
{
GtkWidget* make_linux_pairing_qr(std::string_view payload, int display_size)
{
  if (payload.empty())
    throw std::invalid_argument("Pairing payload is empty");
  const std::string value(payload);
  std::unique_ptr<QRcode, decltype(&QRcode_free)> qr(
      QRcode_encodeString(value.c_str(), 0, QR_ECLEVEL_M, QR_MODE_8, 1), QRcode_free);
  if (!qr)
    throw std::runtime_error("Could not encode pairing QR");

  constexpr int quiet_modules = 4;
  const int size = qr->width + quiet_modules * 2;
  const int stride = size * 4;
  auto pixels = std::make_unique<std::vector<guint8>>(
      static_cast<std::size_t>(stride) * static_cast<std::size_t>(size), 255);
  for (int y = 0; y < qr->width; ++y)
  {
    for (int x = 0; x < qr->width; ++x)
    {
      if ((qr->data[y * qr->width + x] & 1) == 0)
        continue;
      const auto pixel = static_cast<std::size_t>((y + quiet_modules) * stride) +
                         static_cast<std::size_t>((x + quiet_modules) * 4);
      (*pixels)[pixel] = 0;
      (*pixels)[pixel + 1] = 0;
      (*pixels)[pixel + 2] = 0;
    }
  }
  auto* owned_pixels = pixels.release();
  auto* bytes = g_bytes_new_with_free_func(
      owned_pixels->data(),
      owned_pixels->size(),
      [](gpointer data) { delete static_cast<std::vector<guint8>*>(data); },
      owned_pixels);
  auto* texture =
      gdk_memory_texture_new(size, size, GDK_MEMORY_R8G8B8A8, bytes, static_cast<gsize>(stride));
  auto* image = gtk_image_new_from_paintable(GDK_PAINTABLE(texture));
  gtk_widget_set_size_request(image, display_size, display_size);
  g_object_unref(texture);
  g_bytes_unref(bytes);
  return image;
}
} // namespace studio_remote::linux
