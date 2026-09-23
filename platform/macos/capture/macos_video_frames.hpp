#pragma once

#include "../ipc/macos_ipc_names.hpp"
#include "../ipc/read_only_shared_memory.hpp"

#include <optional>
#include <studio_remote/media/video_frame_source.hpp>

namespace studio_remote::macos
{
/// Capture-worker-owned read-only view. Reconnect policy belongs to the capture worker.
class MacosVideoFrames final : public IVideoFrameSource
{
public:
  static std::optional<MacosVideoFrames> open(std::string_view name = ipc_names::video_frames);
  MacosVideoFrames(MacosVideoFrames&&) noexcept = default;
  MacosVideoFrames(const MacosVideoFrames&) = delete;
  MacosVideoFrames& operator=(const MacosVideoFrames&) = delete;
  MacosVideoFrames& operator=(MacosVideoFrames&&) = delete;
  std::optional<VideoProducerSnapshot> producer() const override;
  std::optional<VideoPublicationCounts> published_frames() const override;
  ipc::VideoFrameReadResult read(CapturedVideoTrack track,
                                 ipc::VideoSlotSequenceTracker& sequences,
                                 std::uint64_t activation_published_frames) const override;

private:
  explicit MacosVideoFrames(ReadOnlySharedMemory region);
  ReadOnlySharedMemory region_;
};
} // namespace studio_remote::macos
