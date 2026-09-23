#pragma once

#include <cstdint>
#include <optional>
#include <studio_remote/media/media.hpp>

namespace studio_remote::windows
{

struct NdiBgraFrameView
{
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  float picture_aspect_ratio = 0.0F;
  std::uint64_t captured_ns = 0;
  const std::uint8_t* pixels = nullptr;
  double canvas_aspect_ratio = 0.0;
};

// Copies a vMix NDI BGRA/BGRX frame while preserving its coded square-pixel geometry. vMix's NDI
// picture-aspect metadata is diagnostic only because it can conflict with the actual pixel raster.
std::optional<RawVideoFrame> normalize_ndi_bgra_frame(const NdiBgraFrameView& source);

} // namespace studio_remote::windows
