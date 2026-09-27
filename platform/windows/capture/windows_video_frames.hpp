#pragma once

#include <studio_remote/media/video_frame_source.hpp>

#include "../ipc/windows_ipc_mapping.hpp"
#include <optional>

namespace studio_remote::windows
{
/// Capture-worker-owned read-only view. Partial opens release through RAII. Reconnect, track
/// sequencing and callbacks belong to the worker; returned frames own their pixels independently.
class WindowsVideoFrames final : public IVideoFrameSource
{
public:
  static std::optional<WindowsVideoFrames> open();
  ~WindowsVideoFrames() override;
  WindowsVideoFrames(WindowsVideoFrames&& other) noexcept;
  WindowsVideoFrames(const WindowsVideoFrames&) = delete;
  WindowsVideoFrames& operator=(const WindowsVideoFrames&) = delete;
  WindowsVideoFrames& operator=(WindowsVideoFrames&&) = delete;

  std::optional<VideoProducerSnapshot> producer() const override;
  std::optional<VideoPublicationCounts> published_frames() const override;
  ipc::VideoFrameReadResult read(CapturedVideoTrack track,
                                 ipc::VideoSlotSequenceTracker& sequences,
                                 std::uint64_t activation_published_frames) const override;

private:
  WindowsVideoFrames() = default;
  IpcMappingHandle handle_;
  const ipc::VideoFrameMapping* mapping_ = nullptr;
};
} // namespace studio_remote::windows
