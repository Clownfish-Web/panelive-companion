#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace studio_remote::obs_plugin
{

struct ThumbnailPixelStats
{
  bool black = true;
  double average_channel = 0.0;
  std::size_t non_black_pixels = 0;
  std::size_t total_pixels = 0;
};

inline ThumbnailPixelStats analyze_thumbnail_pixels(std::span<const std::uint8_t> bgra)
{
  ThumbnailPixelStats result;
  result.total_pixels = bgra.size() / 4;
  std::uint64_t channel_sum = 0;
  for (std::size_t offset = 0; offset + 3 < bgra.size(); offset += 4)
  {
    const auto pixel_sum =
        static_cast<std::uint32_t>(bgra[offset]) + bgra[offset + 1] + bgra[offset + 2];
    channel_sum += pixel_sum;
    result.non_black_pixels += pixel_sum > 12;
  }
  if (result.total_pixels != 0)
    result.average_channel =
        static_cast<double>(channel_sum) / (static_cast<double>(result.total_pixels) * 3.0);
  result.black = result.non_black_pixels == 0;
  return result;
}

inline bool thumbnail_pixels_ready(std::string_view source_kind, const ThumbnailPixelStats& stats)
{
  return source_kind != "monitor_capture" || !stats.black;
}

} // namespace studio_remote::obs_plugin
