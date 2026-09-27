#include "linux_video_frames.hpp"

#include <utility>

namespace studio_remote::linux
{
std::optional<LinuxVideoFrames> LinuxVideoFrames::open(std::string name)
{
  LinuxVideoFrames result(
      IpcMapping::open(name, sizeof(ipc::VideoFrameMapping), IpcAccess::read_only));
  if (!result.mapping_ || !ipc::video_frame_mapping_is_compatible(*result.mapping_))
    return std::nullopt;
  return result;
}

LinuxVideoFrames::LinuxVideoFrames(IpcMapping memory)
    : memory_(std::move(memory)),
      mapping_(static_cast<const ipc::VideoFrameMapping*>(memory_.get()))
{
}

LinuxVideoFrames::LinuxVideoFrames(LinuxVideoFrames&& other) noexcept
    : memory_(std::move(other.memory_)), mapping_(std::exchange(other.mapping_, nullptr))
{
}

LinuxVideoFrames::~LinuxVideoFrames() = default;

std::optional<VideoProducerSnapshot> LinuxVideoFrames::producer() const
{
  return mapping_ ? ipc::read_video_producer(*mapping_) : std::nullopt;
}

std::optional<VideoPublicationCounts> LinuxVideoFrames::published_frames() const
{
  return mapping_ ? ipc::read_video_publication_counts(*mapping_) : std::nullopt;
}

ipc::VideoFrameReadResult LinuxVideoFrames::read(CapturedVideoTrack track,
                                                 ipc::VideoSlotSequenceTracker& sequences,
                                                 std::uint64_t activation_published_frames) const
{
  return mapping_ ? ipc::read_video_frame(*mapping_, track, sequences, activation_published_frames)
                  : ipc::VideoFrameReadResult{};
}
} // namespace studio_remote::linux
