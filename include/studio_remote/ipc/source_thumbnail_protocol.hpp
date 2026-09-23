#pragma once

#include <studio_remote/ipc/source_thumbnail_mapping.hpp>
#include <studio_remote/media/thumbnail.hpp>

#include <chrono>
#include <functional>

namespace studio_remote::ipc
{
struct CapturedThumbnail
{
  ThumbnailStatus status = ThumbnailStatus::render_failed;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  std::uint64_t captured_ns = 0;
  std::vector<std::uint8_t> bgra;
};

struct SourceThumbnailTiming
{
  std::function<std::chrono::steady_clock::time_point()> now;
  std::function<void(std::chrono::milliseconds)> pause;
};

void validate_source_thumbnail_timeout(std::chrono::milliseconds timeout);

/// One ThumbnailService worker serializes requests. The native adapter owns the writable mapping
/// for this entire synchronous call; timing callbacks are borrowed and never retained. No native
/// resource, thread, queue or global sequence is owned here. Returned pixels are independent.
/// Raw diagnostics preserve numeric helper status; invalid success metadata exposes no pixels.
CapturedThumbnail capture_source_thumbnail_raw(SourceThumbnailMapping& mapping,
                                               const ThumbnailCaptureRequest& request,
                                               std::chrono::milliseconds timeout,
                                               const SourceThumbnailTiming& timing);
RawThumbnail project_source_thumbnail(CapturedThumbnail result,
                                      const ThumbnailCaptureRequest& request);

} // namespace studio_remote::ipc
