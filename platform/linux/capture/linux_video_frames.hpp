#pragma once

#include <studio_remote/media/video_frame_source.hpp>

#include "../ipc/linux_ipc_mapping.hpp"
#include <optional>

namespace studio_remote::linux
{
/// Capture-worker-owned read-only view. Partial opens release through RAII. Reconnect, track
/// sequencing and callbacks belong to the worker; returned frames own their pixels independently.
class LinuxVideoFrames final : public IVideoFrameSource
{
public:
  static std::optional<LinuxVideoFrames>
  open(std::string name = ipc::video_frame_shared_memory_name);
  ~LinuxVideoFrames() override;
  LinuxVideoFrames(LinuxVideoFrames&& other) noexcept;
  LinuxVideoFrames(const LinuxVideoFrames&) = delete;
  LinuxVideoFrames& operator=(const LinuxVideoFrames&) = delete;
  LinuxVideoFrames& operator=(LinuxVideoFrames&&) = delete;

  std::optional<VideoProducerSnapshot> producer() const override;
  std::optional<VideoPublicationCounts> published_frames() const override;
  ipc::VideoFrameReadResult read(CapturedVideoTrack track,
                                 ipc::VideoSlotSequenceTracker& sequences,
                                 std::uint64_t activation_published_frames) const override;

private:
  explicit LinuxVideoFrames(IpcMapping memory);
  IpcMapping memory_;
  const ipc::VideoFrameMapping* mapping_ = nullptr;
};
} // namespace studio_remote::linux
