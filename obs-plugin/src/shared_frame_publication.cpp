#include "shared_frame_publisher.hpp"

#include <cstring>

namespace studio_remote::obs_plugin
{
bool SharedFramePublisher::publish(VideoTrack track,
                                   const std::uint8_t* pixels,
                                   std::uint32_t width,
                                   std::uint32_t height,
                                   std::uint32_t source_stride,
                                   std::uint64_t captured_ns)
{
  const std::uint32_t target_stride = width * ipc::bytes_per_pixel;
  const std::size_t data_size = static_cast<std::size_t>(target_stride) * height;
  if (!mapping_ || !pixels || width == 0 || height == 0 || width > ipc::maximum_frame_width ||
      height > ipc::maximum_frame_height || source_stride < target_stride ||
      data_size > ipc::maximum_frame_bytes)
  {
    return false;
  }

  ipc::VideoTrackMapping& track_mapping = mapping_for(track);
  const std::uint32_t published = track_mapping.published_slot.load(std::memory_order_relaxed);
  const std::uint32_t current_slot = ipc::video_published_slot_index(published);
  const std::uint32_t next_slot = (current_slot + 1) % 2;
  ipc::VideoFrameSlot& slot = track_mapping.slots[next_slot];

  std::uint64_t sequence = slot.sequence.load(std::memory_order_relaxed);
  if ((sequence & 1U) != 0)
  {
    ++sequence;
  }
  slot.sequence.store(sequence + 1, std::memory_order_release);
  slot.captured_ns = captured_ns;
  slot.width = width;
  slot.height = height;
  slot.stride = target_stride;
  slot.data_size = static_cast<std::uint32_t>(data_size);

  for (std::uint32_t row = 0; row < height; ++row)
  {
    std::memcpy(slot.pixels + static_cast<std::size_t>(row) * target_stride,
                pixels + static_cast<std::size_t>(row) * source_stride,
                target_stride);
  }

  slot.sequence.store(sequence + 2, std::memory_order_release);
  track_mapping.published_slot.store((published & ~1U) | next_slot, std::memory_order_release);
  track_mapping.published_frames.fetch_add(1, std::memory_order_relaxed);
  mapping_->producer_heartbeat_ns.store(captured_ns, std::memory_order_release);
  return true;
}

ipc::VideoCaptureDemandSnapshot SharedFramePublisher::demand_snapshot(std::uint64_t now_ns) const
{
  if (!control_ || !ipc::video_capture_control_is_compatible(*control_))
    return {};
  return ipc::read_video_capture_demand(*control_, now_ns);
}

void SharedFramePublisher::reset_capture_generation()
{
  if (!mapping_)
    return;
  const auto reset_track = [](ipc::VideoTrackMapping& track)
  {
    const auto next_generation =
        ipc::next_video_capture_generation(track.published_slot.load(std::memory_order_relaxed));
    for (auto& slot : track.slots)
    {
      auto sequence = slot.sequence.load(std::memory_order_relaxed);
      if ((sequence & 1U) == 0)
        ++sequence;
      slot.sequence.store(sequence, std::memory_order_release);
      slot.captured_ns = 0;
      slot.width = 0;
      slot.height = 0;
      slot.stride = 0;
      slot.data_size = 0;
      slot.sequence.store(0, std::memory_order_release);
    }
    track.published_slot.store(next_generation, std::memory_order_release);
  };
  reset_track(mapping_->program);
  reset_track(mapping_->preview);
}

ipc::VideoTrackMapping& SharedFramePublisher::mapping_for(VideoTrack track)
{
  return track == VideoTrack::program ? mapping_->program : mapping_->preview;
}

} // namespace studio_remote::obs_plugin
