#pragma once

#include <studio_remote/ipc/source_thumbnail_protocol.hpp>
#include <studio_remote/media/thumbnail.hpp>

#include <chrono>

namespace studio_remote::windows
{
/// One ThumbnailService worker serializes requests and joins before destroying this consumer.
/// Each capture owns its native mapping until the bounded shared transaction returns.
class WindowsThumbnailCapture final : public IThumbnailCapture
{
public:
  explicit WindowsThumbnailCapture(std::chrono::milliseconds timeout = std::chrono::seconds(3));
  RawThumbnail capture(const ThumbnailCaptureRequest& request) override;
  ipc::CapturedThumbnail capture(std::string_view source_id,
                                 std::string_view source_name,
                                 std::uint32_t maximum_width,
                                 std::uint32_t maximum_height,
                                 std::chrono::milliseconds timeout = std::chrono::seconds(3));

private:
  const std::chrono::milliseconds timeout_;
};
} // namespace studio_remote::windows
