#include "macos_video_frames.hpp"

#include <utility>

namespace studio_remote::macos
{
std::optional<MacosVideoFrames> MacosVideoFrames::open(std::string_view name)
{
  auto region = ReadOnlySharedMemory::open(name, sizeof(ipc::VideoFrameMapping));
  if (!region)
    return std::nullopt;
  const auto* mapping = reinterpret_cast<const ipc::VideoFrameMapping*>(region->bytes().data());
  if (!ipc::video_frame_mapping_is_compatible(*mapping))
    return std::nullopt;
  return MacosVideoFrames(std::move(*region));
}
MacosVideoFrames::MacosVideoFrames(ReadOnlySharedMemory region) : region_(std::move(region))
{
}
std::optional<VideoProducerSnapshot> MacosVideoFrames::producer() const
{
  if (region_.bytes().empty())
    return {};
  const auto& mapping = *reinterpret_cast<const ipc::VideoFrameMapping*>(region_.bytes().data());
  return ipc::read_video_producer(mapping);
}
std::optional<VideoPublicationCounts> MacosVideoFrames::published_frames() const
{
  if (region_.bytes().empty())
    return std::nullopt;
  const auto& mapping = *reinterpret_cast<const ipc::VideoFrameMapping*>(region_.bytes().data());
  return ipc::read_video_publication_counts(mapping);
}
ipc::VideoFrameReadResult MacosVideoFrames::read(CapturedVideoTrack track,
                                                 ipc::VideoSlotSequenceTracker& sequences,
                                                 std::uint64_t activation_published_frames) const
{
  if (region_.bytes().empty())
    return {};
  const auto& mapping = *reinterpret_cast<const ipc::VideoFrameMapping*>(region_.bytes().data());
  return ipc::read_video_frame(mapping, track, sequences, activation_published_frames);
}
} // namespace studio_remote::macos
