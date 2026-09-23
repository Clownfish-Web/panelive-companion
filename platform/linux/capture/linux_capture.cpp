#include "linux_capture.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace studio_remote::linux
{
namespace
{
constexpr auto polling_interval = std::chrono::milliseconds(4);
constexpr auto reconnect_interval = std::chrono::milliseconds(250);
constexpr std::uint64_t unavailable_after_ns = 2'000'000'000ULL;

std::uint64_t monotonic_time_ns()
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

bool descriptor_is_owner_restricted(int descriptor, std::size_t expected_size)
{
  struct stat status
  {
  };
  return fstat(descriptor, &status) == 0 && status.st_uid == geteuid() &&
         (status.st_mode & 0077) == 0 && status.st_size >= static_cast<off_t>(expected_size);
}
} // namespace

CaptureManager::CaptureManager(TrackHandler track_handler,
                               HealthHandler health_handler,
                               DemandProvider demand_provider,
                               GenerationHandler generation_handler)
    : track_handler_(std::move(track_handler)), health_handler_(std::move(health_handler)),
      demand_provider_(std::move(demand_provider)),
      generation_handler_(std::move(generation_handler))
{
}

CaptureManager::~CaptureManager()
{
  stop();
}

void CaptureManager::start()
{
  if (thread_.joinable())
    return;
  stopping_ = false;
  thread_ = std::thread([this] { capture_loop(); });
}

void CaptureManager::stop()
{
  stopping_ = true;
  if (thread_.joinable())
    thread_.join();
  if (control_)
    publish_demand({}, monotonic_time_ns(), true);
  disconnect_mapping();
}

void CaptureManager::capture_loop()
{
  while (!stopping_)
  {
    if (!mapping_ && !connect_mapping())
    {
      std::this_thread::sleep_for(reconnect_interval);
      continue;
    }
    if (mapping_->magic != ipc::video_frame_magic ||
        mapping_->version != ipc::video_frame_mapping_version ||
        mapping_->structure_size != sizeof(ipc::VideoFrameMapping))
    {
      disconnect_mapping();
      continue;
    }
    if (!observe_generation())
    {
      std::this_thread::sleep_for(polling_interval);
      continue;
    }
    auto demand = demand_provider_ ? demand_provider_() : VideoDemand{};
    demand.requested_program_fps =
        std::min(demand.requested_program_fps, ipc::maximum_requested_video_fps);
    demand.requested_preview_fps =
        std::min(demand.requested_preview_fps, ipc::maximum_requested_video_fps);
    if (demand.active_video_client_count == 0)
      demand = {};
    {
      std::lock_guard lock(mutex_);
      if (current_demand_.active_video_client_count == 0 && demand.active_video_client_count != 0)
      {
        program_.sequences.reset_identity();
        preview_.sequences.reset_identity();
        program_.activation_published_frames =
            mapping_->program.published_frames.load(std::memory_order_acquire);
        preview_.activation_published_frames =
            mapping_->preview.published_frames.load(std::memory_order_acquire);
      }
      current_demand_ = demand;
      producer_process_id_ = mapping_->producer_process_id;
    }
    const auto now = monotonic_time_ns();
    publish_demand(demand, now, false);
    if (demand.active_video_client_count != 0)
    {
      read_track("program", mapping_->program, program_);
      read_track("preview", mapping_->preview, preview_);
    }
    publish_health("program", program_);
    publish_health("preview", preview_);
    std::this_thread::sleep_for(polling_interval);
  }
}

bool CaptureManager::connect_mapping()
{
  mapping_descriptor_ = shm_open(ipc::video_frame_shared_memory_name, O_RDONLY | O_CLOEXEC, 0);
  if (mapping_descriptor_ < 0 ||
      !descriptor_is_owner_restricted(mapping_descriptor_, sizeof(ipc::VideoFrameMapping)))
  {
    disconnect_mapping();
    return false;
  }
  auto* frame_address =
      mmap(nullptr, sizeof(ipc::VideoFrameMapping), PROT_READ, MAP_SHARED, mapping_descriptor_, 0);
  if (frame_address == MAP_FAILED)
  {
    disconnect_mapping();
    return false;
  }
  mapping_ = static_cast<const ipc::VideoFrameMapping*>(frame_address);
  control_descriptor_ =
      shm_open(ipc::video_capture_control_shared_memory_name, O_RDWR | O_CLOEXEC, 0);
  if (control_descriptor_ < 0 ||
      !descriptor_is_owner_restricted(control_descriptor_, sizeof(ipc::VideoCaptureControlMapping)))
  {
    disconnect_mapping();
    return false;
  }
  auto* control_address = mmap(nullptr,
                               sizeof(ipc::VideoCaptureControlMapping),
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED,
                               control_descriptor_,
                               0);
  if (control_address == MAP_FAILED)
  {
    disconnect_mapping();
    return false;
  }
  control_ = static_cast<ipc::VideoCaptureControlMapping*>(control_address);
  if (!ipc::video_capture_control_is_compatible(*control_))
  {
    disconnect_mapping();
    return false;
  }
  {
    std::lock_guard lock(mutex_);
    mapping_connected_ = true;
  }
  published_demand_.reset();
  capture_generation_.reset();
  demand_generation_ = control_->demand_generation.load(std::memory_order_acquire);
  if (demand_generation_ & 1U)
    ++demand_generation_;
  return true;
}

void CaptureManager::disconnect_mapping()
{
  {
    std::lock_guard lock(mutex_);
    mapping_connected_ = false;
    producer_process_id_ = 0;
    current_demand_ = {};
  }
  if (control_)
    munmap(control_, sizeof(ipc::VideoCaptureControlMapping));
  if (control_descriptor_ >= 0)
    close(control_descriptor_);
  control_ = nullptr;
  control_descriptor_ = -1;
  if (mapping_)
    munmap(const_cast<ipc::VideoFrameMapping*>(mapping_), sizeof(ipc::VideoFrameMapping));
  if (mapping_descriptor_ >= 0)
    close(mapping_descriptor_);
  mapping_ = nullptr;
  mapping_descriptor_ = -1;
}

bool CaptureManager::observe_generation()
{
  const auto program = ipc::video_capture_generation(
      mapping_->program.published_slot.load(std::memory_order_acquire));
  const auto preview = ipc::video_capture_generation(
      mapping_->preview.published_slot.load(std::memory_order_acquire));
  if (program != preview)
    return false;
  bool changed = false;
  {
    std::lock_guard lock(mutex_);
    if (!capture_generation_)
      capture_generation_ = program;
    else if (*capture_generation_ != program)
    {
      capture_generation_ = program;
      program_.sequences.reset_identity();
      preview_.sequences.reset_identity();
      changed = true;
    }
  }
  if (changed && generation_handler_)
    generation_handler_(program);
  return true;
}

void CaptureManager::publish_demand(const VideoDemand& demand, std::uint64_t now_ns, bool force)
{
  if (!control_)
    return;
  if (!published_demand_ || *published_demand_ != demand)
  {
    control_->demand_generation.store(demand_generation_ + 1, std::memory_order_release);
    control_->active_video_client_count.store(demand.active_video_client_count,
                                              std::memory_order_relaxed);
    control_->requested_program_fps.store(demand.requested_program_fps, std::memory_order_relaxed);
    control_->requested_preview_fps.store(demand.requested_preview_fps, std::memory_order_relaxed);
    demand_generation_ += 2;
    control_->demand_generation.store(demand_generation_, std::memory_order_release);
    published_demand_ = demand;
    force = true;
  }
  if (force ||
      now_ns - last_demand_heartbeat_ns_ >= ipc::video_capture_demand_heartbeat_interval_ns)
  {
    control_->companion_heartbeat_ns.store(now_ns, std::memory_order_release);
    last_demand_heartbeat_ns_ = now_ns;
  }
}

void CaptureManager::read_track(std::string_view name,
                                const ipc::VideoTrackMapping& mapping,
                                TrackState& state)
{
  std::unique_lock lock(mutex_);
  const auto slot_index =
      ipc::video_published_slot_index(mapping.published_slot.load(std::memory_order_acquire));
  if (mapping.published_frames.load(std::memory_order_acquire) <= state.activation_published_frames)
    return;
  const auto& slot = mapping.slots[slot_index];
  const auto sequence = slot.sequence.load(std::memory_order_acquire);
  if ((sequence & 1U) || !state.sequences.is_unseen(slot_index, sequence))
    return;
  const auto expected_stride = slot.width * ipc::bytes_per_pixel;
  const auto expected_size = static_cast<std::size_t>(expected_stride) * slot.height;
  if (slot.width == 0 || slot.height == 0 || slot.width > ipc::maximum_frame_width ||
      slot.height > ipc::maximum_frame_height || slot.stride != expected_stride ||
      slot.data_size != expected_size || expected_size > ipc::maximum_frame_bytes)
  {
    ++state.dropped_frames;
    return;
  }
  auto frame = std::make_shared<RawVideoFrame>();
  frame->width = slot.width;
  frame->height = slot.height;
  frame->stride = slot.stride;
  frame->captured_ns = slot.captured_ns;
  frame->bgra.resize(slot.data_size);
  std::memcpy(frame->bgra.data(), slot.pixels, slot.data_size);
  const auto sequence_after = slot.sequence.load(std::memory_order_acquire);
  if (sequence != sequence_after || (sequence_after & 1U))
  {
    ++state.dropped_frames;
    return;
  }
  const auto observation = state.sequences.accept(slot_index, sequence_after, frame->captured_ns);
  state.dropped_frames += observation.dropped_frames;
  if (!observation.publish)
    return;
  state.last_frame_ns = monotonic_time_ns();
  ++state.received_frames;
  lock.unlock();
  track_handler_(name, std::move(frame));
}

void CaptureManager::publish_health(std::string_view name, TrackState& state)
{
  std::unique_lock lock(mutex_);
  const auto now = monotonic_time_ns();
  const auto available = current_demand_.active_video_client_count != 0 &&
                         state.last_frame_ns != 0 &&
                         now - state.last_frame_ns < unavailable_after_ns;
  if (available == state.previously_available)
    return;
  state.previously_available = available;
  const json health = {{"available", available},
                       {"backend", "obs-plugin-posix-shared-memory"},
                       {"capturedFrames", state.received_frames},
                       {"drops", state.dropped_frames}};
  lock.unlock();
  if (health_handler_)
    health_handler_(name, health);
}

json CaptureManager::health() const
{
  std::lock_guard lock(mutex_);
  return {{"ipcConnected", mapping_connected_},
          {"producerPid", producer_process_id_},
          {"activeVideoClientCount", current_demand_.active_video_client_count},
          {"demandGeneration", demand_generation_},
          {"captureGeneration", capture_generation_ ? json(*capture_generation_) : json(nullptr)},
          {"programFrames", program_.received_frames},
          {"previewFrames", preview_.received_frames}};
}
} // namespace studio_remote::linux
