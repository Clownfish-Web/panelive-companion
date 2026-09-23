#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace studio_remote::vmix
{

enum class OutputAspectRatioMode
{
  normal,
  widescreen,
  anamorphic,
  unknown,
};

struct OutputGeometry
{
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  OutputAspectRatioMode aspect_ratio_mode = OutputAspectRatioMode::unknown;

  double canvas_aspect_ratio() const noexcept;
};

// Parses the bounded per-user vMix settings document. Companion preserves the complete OutputSize
// raster requested for recording and streaming; AspectRatio is retained as separate diagnostics.
std::optional<OutputGeometry> parse_output_geometry(std::string_view settings_xml);

} // namespace studio_remote::vmix
