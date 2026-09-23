#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace studio_remote
{
struct PairingQrImage
{
  std::vector<std::uint8_t> pixels;
  int size = 0;
};

PairingQrImage make_pairing_qr_image(std::string_view payload);
/// Resamples the existing pixels without filtering QR module boundaries.
PairingQrImage resample_pairing_qr_image(const PairingQrImage& image, int size);
} // namespace studio_remote
