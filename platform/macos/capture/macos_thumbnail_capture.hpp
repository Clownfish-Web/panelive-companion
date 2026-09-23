#pragma once

#include "../ipc/macos_ipc_names.hpp"

#include <studio_remote/ipc/source_thumbnail_protocol.hpp>
#include <studio_remote/media/thumbnail.hpp>

#include <chrono>
#include <string>

namespace studio_remote::macos
{
/// One ThumbnailService worker owns this consumer and serializes mailbox requests. Capture is
/// bounded by timeout; the service joins its worker before destroying this object.
class MacosThumbnailCapture final : public IThumbnailCapture
{
public:
  explicit MacosThumbnailCapture(std::string mapping_name = ipc_names::source_thumbnails,
                                 std::chrono::milliseconds timeout = std::chrono::seconds(3));
  RawThumbnail capture(const ThumbnailCaptureRequest& request) override;
  ipc::CapturedThumbnail capture(std::string_view source_id,
                                 std::string_view source_name,
                                 std::uint32_t maximum_width,
                                 std::uint32_t maximum_height,
                                 std::chrono::milliseconds timeout = std::chrono::seconds(3));

private:
  const std::string mapping_name_;
  const std::chrono::milliseconds timeout_;
};
} // namespace studio_remote::macos
