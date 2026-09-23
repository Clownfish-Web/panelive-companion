#pragma once

#include <memory>
#include <optional>
#include <studio_remote/ipc/video_frame_mapping.hpp>
#include <studio_remote/ipc/video_slot_sequence_tracker.hpp>
#include <studio_remote/media/media.hpp>
#include <studio_remote/media/video_capture.hpp>

namespace studio_remote::ipc
{
struct VideoFrameReadResult
{
  std::shared_ptr<const RawVideoFrame> frame;
  std::uint64_t dropped_frames = 0;
};

bool video_frame_mapping_is_compatible(const VideoFrameMapping& mapping) noexcept;
std::optional<VideoProducerSnapshot> read_video_producer(const VideoFrameMapping& mapping) noexcept;
std::optional<VideoPublicationCounts>
read_video_publication_counts(const VideoFrameMapping& mapping) noexcept;

/// Revalidates the complete mapping before selecting a track for the existing bounded copier.
VideoFrameReadResult read_video_frame(const VideoFrameMapping& mapping,
                                      CapturedVideoTrack track,
                                      VideoSlotSequenceTracker& sequences,
                                      std::uint64_t activation_published_frames);

/// Copies one stable, bounded BGRA snapshot. The caller owns track sequencing and activation.
/// The returned immutable frame owns its pixels independently of the producer mapping.
VideoFrameReadResult read_video_frame(const VideoTrackMapping& track,
                                      VideoSlotSequenceTracker& sequences,
                                      std::uint64_t activation_published_frames);
} // namespace studio_remote::ipc
