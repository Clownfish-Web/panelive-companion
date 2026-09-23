#include "shared_frame_publisher.hpp"

#include <cstring>
#include <fcntl.h>
#include <new>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace studio_remote::obs_plugin
{
namespace
{
template <typename Mapping> Mapping* create_mapping(const char* name, int& descriptor)
{
  const auto candidate = shm_open(name, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (candidate < 0)
    return nullptr;
  struct stat status
  {
  };
  if (fstat(candidate, &status) != 0 || status.st_uid != geteuid() ||
      fchmod(candidate, 0600) != 0 ||
      ftruncate(candidate, static_cast<off_t>(sizeof(Mapping))) != 0)
  {
    ::close(candidate);
    return nullptr;
  }
  auto* address = mmap(nullptr, sizeof(Mapping), PROT_READ | PROT_WRITE, MAP_SHARED, candidate, 0);
  if (address == MAP_FAILED)
  {
    ::close(candidate);
    return nullptr;
  }
  descriptor = candidate;
  return static_cast<Mapping*>(address);
}
} // namespace

SharedFramePublisher::SharedFramePublisher() = default;

SharedFramePublisher::~SharedFramePublisher()
{
  close();
}

bool SharedFramePublisher::open()
{
  if (mapping_)
    return true;
  mapping_ =
      create_mapping<ipc::VideoFrameMapping>(ipc::video_frame_shared_memory_name, mapping_handle_);
  if (!mapping_)
  {
    close();
    return false;
  }
  std::construct_at(mapping_);
  mapping_->structure_size = sizeof(ipc::VideoFrameMapping);
  mapping_->producer_process_id = static_cast<std::uint32_t>(getpid());
  control_ = create_mapping<ipc::VideoCaptureControlMapping>(
      ipc::video_capture_control_shared_memory_name, control_handle_);
  if (!control_)
  {
    close();
    return false;
  }
  std::construct_at(control_);
  control_->structure_size = sizeof(ipc::VideoCaptureControlMapping);
  control_->producer_process_id = static_cast<std::uint32_t>(getpid());
  return true;
}

void SharedFramePublisher::close()
{
  if (control_)
    munmap(control_, sizeof(ipc::VideoCaptureControlMapping));
  if (control_handle_ >= 0)
    ::close(control_handle_);
  control_ = nullptr;
  control_handle_ = -1;
  if (mapping_)
    munmap(mapping_, sizeof(ipc::VideoFrameMapping));
  if (mapping_handle_ >= 0)
    ::close(mapping_handle_);
  mapping_ = nullptr;
  mapping_handle_ = -1;
}

bool SharedFramePublisher::publish(VideoTrack track,
                                   const std::uint8_t* pixels,
                                   std::uint32_t width,
                                   std::uint32_t height,
                                   std::uint32_t source_stride,
                                   std::uint64_t captured_ns)
{
  const auto target_stride = width * ipc::bytes_per_pixel;
  const auto data_size = static_cast<std::size_t>(target_stride) * height;
  if (!mapping_ || !pixels || width == 0 || height == 0 || width > ipc::maximum_frame_width ||
      height > ipc::maximum_frame_height || source_stride < target_stride ||
      data_size > ipc::maximum_frame_bytes)
    return false;
  auto& track_mapping = mapping_for(track);
  const auto published = track_mapping.published_slot.load(std::memory_order_relaxed);
  const auto next_slot = (ipc::video_published_slot_index(published) + 1U) % 2U;
  auto& slot = track_mapping.slots[next_slot];
  auto sequence = slot.sequence.load(std::memory_order_relaxed);
  if (sequence & 1U)
    ++sequence;
  slot.sequence.store(sequence + 1, std::memory_order_release);
  slot.captured_ns = captured_ns;
  slot.width = width;
  slot.height = height;
  slot.stride = target_stride;
  slot.data_size = static_cast<std::uint32_t>(data_size);
  for (std::uint32_t row = 0; row < height; ++row)
    std::memcpy(slot.pixels + static_cast<std::size_t>(row) * target_stride,
                pixels + static_cast<std::size_t>(row) * source_stride,
                target_stride);
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
  const auto reset = [](ipc::VideoTrackMapping& track)
  {
    const auto generation =
        ipc::next_video_capture_generation(track.published_slot.load(std::memory_order_relaxed));
    for (auto& slot : track.slots)
      std::construct_at(&slot);
    track.published_slot.store(generation, std::memory_order_release);
  };
  reset(mapping_->program);
  reset(mapping_->preview);
}

ipc::VideoTrackMapping& SharedFramePublisher::mapping_for(VideoTrack track)
{
  return track == VideoTrack::program ? mapping_->program : mapping_->preview;
}
} // namespace studio_remote::obs_plugin
