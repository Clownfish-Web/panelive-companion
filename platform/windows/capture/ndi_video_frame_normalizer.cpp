#include "ndi_video_frame_normalizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace studio_remote::windows
{
namespace
{

constexpr std::uint32_t maximum_dimension = 16'384;
constexpr std::uint32_t bytes_per_pixel = 4;
constexpr std::uint32_t maximum_monitor_width = 960;
constexpr std::uint32_t maximum_monitor_height = 480;

double display_aspect_ratio(const NdiBgraFrameView& source)
{
  // vMix can advertise a 16:9 NDI picture aspect for a square-pixel 16:10 raster. The pixels are
  // authoritative for this provider because respecting that conflicting metadata distorts them.
  return static_cast<double>(source.width) / source.height;
}

std::pair<std::uint32_t, std::uint32_t> normalized_dimensions(const NdiBgraFrameView& source)
{
  const auto aspect_ratio = display_aspect_ratio(source);
  const auto width_limited_height = static_cast<double>(maximum_monitor_width) / aspect_ratio;
  const auto target_height = std::min({static_cast<double>(source.height),
                                       static_cast<double>(maximum_monitor_height),
                                       width_limited_height});
  const auto even_height = static_cast<std::uint32_t>(target_height) & ~1U;
  if (even_height < 2)
    return {};

  const auto even_width = std::lround(even_height * aspect_ratio / 2.0) * 2;
  if (even_width < 2 || even_width > maximum_dimension)
    return {};
  return {static_cast<std::uint32_t>(even_width), even_height};
}

void resize_nearest(const NdiBgraFrameView& source, RawVideoFrame& destination)
{
  if (source.width == destination.width && source.height == destination.height)
  {
    const auto row_bytes = static_cast<std::size_t>(destination.stride);
    for (std::uint32_t y = 0; y < destination.height; ++y)
      std::memcpy(destination.bgra.data() + static_cast<std::size_t>(y) * destination.stride,
                  source.pixels + static_cast<std::size_t>(y) * source.stride,
                  row_bytes);
    return;
  }

  std::vector<std::uint32_t> source_offsets(destination.width);
  for (std::uint32_t x = 0; x < destination.width; ++x)
    source_offsets[x] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * source.width /
                                                   destination.width * bytes_per_pixel);

  for (std::uint32_t y = 0; y < destination.height; ++y)
  {
    const auto source_y = static_cast<std::uint64_t>(y) * source.height / destination.height;
    const auto* source_row = source.pixels + source_y * source.stride;
    auto* destination_row =
        destination.bgra.data() + static_cast<std::size_t>(y) * destination.stride;
    for (std::uint32_t x = 0; x < destination.width; ++x)
    {
      const auto* source_pixel = source_row + source_offsets[x];
      auto* destination_pixel = destination_row + static_cast<std::size_t>(x) * bytes_per_pixel;
      std::memcpy(destination_pixel, source_pixel, bytes_per_pixel);
    }
  }
}

} // namespace

std::optional<RawVideoFrame> normalize_ndi_bgra_frame(const NdiBgraFrameView& source)
{
  if (source.width == 0 || source.height == 0 || source.width > maximum_dimension ||
      source.height > maximum_dimension || !source.pixels ||
      source.stride < source.width * bytes_per_pixel)
    return std::nullopt;

  RawVideoFrame destination;
  const auto [width, height] = normalized_dimensions(source);
  destination.width = width;
  destination.height = height;
  if (destination.width == 0 || destination.height == 0)
    return std::nullopt;
  destination.stride = destination.width * bytes_per_pixel;
  destination.captured_ns = source.captured_ns;
  if (destination.height > std::numeric_limits<std::size_t>::max() / destination.stride)
    return std::nullopt;
  destination.bgra.resize(static_cast<std::size_t>(destination.stride) * destination.height);

  resize_nearest(source, destination);
  return destination;
}

} // namespace studio_remote::windows
