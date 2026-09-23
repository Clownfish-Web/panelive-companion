#include <studio_remote/ipc/video_frame_reader.hpp>

#include <cstring>
#include <utility>

namespace studio_remote::ipc
{
bool video_frame_mapping_is_compatible(const VideoFrameMapping& mapping) noexcept
{
  return mapping.magic == video_frame_magic && mapping.version == video_frame_mapping_version &&
         mapping.structure_size == sizeof(VideoFrameMapping);
}

std::optional<VideoProducerSnapshot> read_video_producer(const VideoFrameMapping& mapping) noexcept
{
  if (!video_frame_mapping_is_compatible(mapping))
    return std::nullopt;
  const auto program_generation =
      video_capture_generation(mapping.program.published_slot.load(std::memory_order_acquire));
  const auto preview_generation =
      video_capture_generation(mapping.preview.published_slot.load(std::memory_order_acquire));
  return VideoProducerSnapshot{mapping.producer_process_id,
                               mapping.producer_heartbeat_ns.load(std::memory_order_acquire),
                               program_generation,
                               preview_generation};
}

std::optional<VideoPublicationCounts>
read_video_publication_counts(const VideoFrameMapping& mapping) noexcept
{
  if (!video_frame_mapping_is_compatible(mapping))
    return std::nullopt;
  return VideoPublicationCounts{mapping.program.published_frames.load(std::memory_order_acquire),
                                mapping.preview.published_frames.load(std::memory_order_acquire)};
}

VideoFrameReadResult read_video_frame(const VideoFrameMapping& mapping,
                                      CapturedVideoTrack track,
                                      VideoSlotSequenceTracker& sequences,
                                      std::uint64_t activation_published_frames)
{
  if (!video_frame_mapping_is_compatible(mapping))
    return {};
  return read_video_frame(track == CapturedVideoTrack::program ? mapping.program : mapping.preview,
                          sequences,
                          activation_published_frames);
}

VideoFrameReadResult read_video_frame(const VideoTrackMapping& track,
                                      VideoSlotSequenceTracker& sequences,
                                      std::uint64_t activation_published_frames)
{
  const auto published = track.published_slot.load(std::memory_order_acquire);
  const auto slot_index = video_published_slot_index(published);
  if (track.published_frames.load(std::memory_order_acquire) <= activation_published_frames)
    return {};
  const auto& slot = track.slots[slot_index];
  const auto before = slot.sequence.load(std::memory_order_acquire);
  if ((before & 1U) != 0 || !sequences.is_unseen(slot_index, before))
    return {};

  // Snapshot metadata once. A writer can change shared fields before the final sequence check;
  // using a newly read data_size for memcpy would defeat the bounds already validated here.
  const auto width = slot.width;
  const auto height = slot.height;
  const auto stride = slot.stride;
  const auto size = slot.data_size;
  const auto captured_ns = slot.captured_ns;
  if (width == 0 || height == 0 || width > maximum_frame_width || height > maximum_frame_height)
    return {nullptr, 1};
  const auto expected_stride = width * bytes_per_pixel;
  const auto expected_size = static_cast<std::size_t>(expected_stride) * height;
  if (stride != expected_stride || size != expected_size || expected_size > maximum_frame_bytes)
    return {nullptr, 1};

  auto frame = std::make_shared<RawVideoFrame>();
  frame->width = width;
  frame->height = height;
  frame->stride = stride;
  frame->captured_ns = captured_ns;
  frame->bgra.resize(size);
  std::memcpy(frame->bgra.data(), slot.pixels, size);
  const auto after = slot.sequence.load(std::memory_order_acquire);
  const auto final_published = track.published_slot.load(std::memory_order_acquire);
  if (before != after || (after & 1U) != 0 ||
      video_capture_generation(published) != video_capture_generation(final_published))
    return {nullptr, 1};
  const auto observation = sequences.accept(slot_index, after, captured_ns);
  return {observation.publish ? std::move(frame) : nullptr, observation.dropped_frames};
}
} // namespace studio_remote::ipc
