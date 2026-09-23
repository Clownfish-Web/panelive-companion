#pragma once

#include <studio_remote/ipc/video_frame_reader.hpp>

namespace studio_remote
{
/// Uniquely owned by one capture worker, which serializes all resource operations. Returned
/// frames own their pixels independently; sequence state belongs to the worker, not the resource.
class IVideoFrameSource
{
public:
  virtual ~IVideoFrameSource() = default;
  virtual std::optional<VideoProducerSnapshot> producer() const = 0;
  virtual std::optional<VideoPublicationCounts> published_frames() const = 0;
  virtual ipc::VideoFrameReadResult read(CapturedVideoTrack track,
                                         ipc::VideoSlotSequenceTracker& sequences,
                                         std::uint64_t activation_published_frames) const = 0;
};
} // namespace studio_remote
