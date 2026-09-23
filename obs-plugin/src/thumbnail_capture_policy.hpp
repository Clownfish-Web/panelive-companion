#pragma once

#include <string_view>

namespace studio_remote::obs_plugin
{

enum class ThumbnailCapturePolicy
{
  showing_only_render,
  require_existing_showing
};

struct ThumbnailSourceSettings
{
  bool deactivate_when_not_showing = false;
  bool shutdown_when_not_visible = false;
};

// Sources in the second class cannot create a new frame merely by becoming showing. Starting their
// producer would require Program-equivalent activation or explicit playback control, both of which
// are forbidden for passive thumbnail capture.
inline ThumbnailCapturePolicy
thumbnail_capture_policy(std::string_view source_kind,
                         ThumbnailSourceSettings settings = {}) noexcept
{
  if (source_kind == "ffmpeg_source" || source_kind == "monitor_capture" ||
      source_kind == "game_capture")
    return ThumbnailCapturePolicy::require_existing_showing;
  if (source_kind == "dshow_input" && settings.deactivate_when_not_showing)
    return ThumbnailCapturePolicy::require_existing_showing;
  if (source_kind == "browser_source" && settings.shutdown_when_not_visible)
    return ThumbnailCapturePolicy::require_existing_showing;
  return ThumbnailCapturePolicy::showing_only_render;
}

} // namespace studio_remote::obs_plugin
