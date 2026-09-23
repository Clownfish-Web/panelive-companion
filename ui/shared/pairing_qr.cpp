#include "pairing_qr.hpp"

#include <qrencode.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>

namespace studio_remote
{
PairingQrImage make_pairing_qr_image(std::string_view payload)
{
  if (payload.empty())
    throw std::invalid_argument("Pairing payload is empty");
  if (payload.find('\0') != std::string_view::npos)
    throw std::invalid_argument("Pairing payload contains a null byte");
  const std::string null_terminated_payload(payload);
  std::unique_ptr<QRcode, decltype(&QRcode_free)> qr(
      QRcode_encodeString(null_terminated_payload.c_str(), 0, QR_ECLEVEL_M, QR_MODE_8, 1),
      QRcode_free);
  if (!qr)
    throw std::runtime_error("Could not encode pairing QR");

  constexpr int quiet_modules = 4;
  const int size = qr->width + quiet_modules * 2;
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4, 255);
  for (int y = 0; y < qr->width; ++y)
  {
    for (int x = 0; x < qr->width; ++x)
    {
      if ((qr->data[y * qr->width + x] & 1) == 0)
        continue;
      const auto pixel = (static_cast<std::size_t>(y + quiet_modules) * size +
                          static_cast<std::size_t>(x + quiet_modules)) *
                         4;
      pixels[pixel] = 0;
      pixels[pixel + 1] = 0;
      pixels[pixel + 2] = 0;
      pixels[pixel + 3] = 255;
    }
  }
  return {std::move(pixels), size};
}

PairingQrImage resample_pairing_qr_image(const PairingQrImage& image, int size)
{
  constexpr int maximum_size = 4096;
  if (image.size <= 0 || image.size > maximum_size || size <= 0 || size > maximum_size ||
      image.pixels.size() != static_cast<std::size_t>(image.size) * image.size * 4)
    throw std::invalid_argument("Pairing QR dimensions are invalid");
  PairingQrImage result{std::vector<std::uint8_t>(static_cast<std::size_t>(size) * size * 4), size};
  for (int y = 0; y < size; ++y)
  {
    const auto source_y = static_cast<std::size_t>(y) * image.size / size;
    for (int x = 0; x < size; ++x)
    {
      const auto source_x = static_cast<std::size_t>(x) * image.size / size;
      const auto source = (source_y * image.size + source_x) * 4;
      const auto target = (static_cast<std::size_t>(y) * size + x) * 4;
      std::copy_n(image.pixels.begin() + source, 4, result.pixels.begin() + target);
    }
  }
  return result;
}
} // namespace studio_remote
