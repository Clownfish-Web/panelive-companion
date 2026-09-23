#include "linux_audio_monitor.hpp"

#include <studio_remote/common/types.hpp>
#include <studio_remote/ipc/audio_monitor_mapping.hpp>
#include <studio_remote/ipc/audio_monitor_protocol.hpp>

#include <chrono>
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace studio_remote::linux
{
namespace
{
constexpr auto polling_interval = std::chrono::milliseconds(5);
constexpr auto reconnect_interval = std::chrono::milliseconds(250);

bool descriptor_is_owner_restricted(int descriptor, std::size_t expected_size)
{
  struct stat status
  {
  };
  return fstat(descriptor, &status) == 0 && status.st_uid == geteuid() &&
         (status.st_mode & 0077) == 0 && status.st_size >= static_cast<off_t>(expected_size);
}

class MappedAudioIpc
{
public:
  ~MappedAudioIpc()
  {
    disconnect();
  }

  MappedAudioIpc(const MappedAudioIpc&) = delete;
  MappedAudioIpc& operator=(const MappedAudioIpc&) = delete;

  MappedAudioIpc() = default;

  bool connect(const LinuxAudioMonitorIpcNames& names)
  {
    mapping_descriptor_ = shm_open(names.audio.c_str(), O_RDONLY | O_CLOEXEC, 0);
    if (mapping_descriptor_ < 0 ||
        !descriptor_is_owner_restricted(mapping_descriptor_, sizeof(ipc::AudioMonitorMapping)))
    {
      disconnect();
      return false;
    }
    auto* mapping_address = mmap(
        nullptr, sizeof(ipc::AudioMonitorMapping), PROT_READ, MAP_SHARED, mapping_descriptor_, 0);
    if (mapping_address == MAP_FAILED)
    {
      disconnect();
      return false;
    }
    mapping_ = static_cast<const ipc::AudioMonitorMapping*>(mapping_address);

    control_descriptor_ = shm_open(names.control.c_str(), O_RDWR | O_CLOEXEC, 0);
    if (control_descriptor_ < 0 ||
        !descriptor_is_owner_restricted(control_descriptor_,
                                        sizeof(ipc::AudioMonitorControlMapping)))
    {
      disconnect();
      return false;
    }
    auto* control_address = mmap(nullptr,
                                 sizeof(ipc::AudioMonitorControlMapping),
                                 PROT_READ | PROT_WRITE,
                                 MAP_SHARED,
                                 control_descriptor_,
                                 0);
    if (control_address == MAP_FAILED)
    {
      disconnect();
      return false;
    }
    control_ = static_cast<ipc::AudioMonitorControlMapping*>(control_address);
    if (!ipc::compatible_audio_monitor_control(*control_))
    {
      disconnect();
      return false;
    }
    return true;
  }

  void disconnect()
  {
    if (control_)
      munmap(control_, sizeof(ipc::AudioMonitorControlMapping));
    if (control_descriptor_ >= 0)
      close(control_descriptor_);
    control_ = nullptr;
    control_descriptor_ = -1;
    if (mapping_)
      munmap(const_cast<ipc::AudioMonitorMapping*>(mapping_), sizeof(ipc::AudioMonitorMapping));
    if (mapping_descriptor_ >= 0)
      close(mapping_descriptor_);
    mapping_ = nullptr;
    mapping_descriptor_ = -1;
  }

  const ipc::AudioMonitorMapping& mapping() const
  {
    return *mapping_;
  }

  ipc::AudioMonitorControlMapping& control() const
  {
    return *control_;
  }

private:
  int mapping_descriptor_ = -1;
  const ipc::AudioMonitorMapping* mapping_ = nullptr;
  int control_descriptor_ = -1;
  ipc::AudioMonitorControlMapping* control_ = nullptr;
};

bool status_changed(const AudioMonitorStatus& left, const AudioMonitorStatus& right)
{
  return left.supported != right.supported || left.available != right.available ||
         left.configured != right.configured || left.reason != right.reason ||
         left.sample_rate != right.sample_rate || left.channels != right.channels ||
         left.dropped_frames != right.dropped_frames ||
         left.ipc_skipped_frames != right.ipc_skipped_frames ||
         left.timestamp_discontinuities != right.timestamp_discontinuities ||
         left.ingress_overflow_drops != right.ingress_overflow_drops ||
         left.oversized_callback_drops != right.oversized_callback_drops ||
         left.callback_count != right.callback_count ||
         left.maximum_ingress_occupancy != right.maximum_ingress_occupancy ||
         left.mixer_deadline_misses != right.mixer_deadline_misses ||
         left.timeline_gaps != right.timeline_gaps ||
         left.timeline_overlaps != right.timeline_overlaps ||
         left.non_finite_samples != right.non_finite_samples ||
         left.limiter_frames != right.limiter_frames ||
         left.ipc_sample_index_gaps != right.ipc_sample_index_gaps ||
         left.ipc_seqlock_retries != right.ipc_seqlock_retries ||
         left.latest_peak != right.latest_peak || left.latest_rms != right.latest_rms ||
         left.latest_dc != right.latest_dc;
}
} // namespace

LinuxAudioMonitor::LinuxAudioMonitor(FrameHandler frame_handler,
                                     StatusHandler status_handler,
                                     LinuxAudioMonitorIpcNames ipc_names)
    : frame_handler_(std::move(frame_handler)), status_handler_(std::move(status_handler)),
      ipc_names_(std::move(ipc_names))
{
}

LinuxAudioMonitor::~LinuxAudioMonitor()
{
  stop();
}

void LinuxAudioMonitor::start()
{
  if (thread_.joinable())
    return;
  stopping_ = false;
  thread_ = std::thread([this] { read_loop(); });
}

void LinuxAudioMonitor::stop()
{
  stopping_ = true;
  if (thread_.joinable())
    thread_.join();
}

AudioMonitorStatus LinuxAudioMonitor::status() const
{
  std::lock_guard lock(status_mutex_);
  return status_;
}

void LinuxAudioMonitor::set_mix_settings(const AudioMonitorMixSettings& settings)
{
  if (!valid_audio_monitor_headroom_db(settings.attenuation_per_additional_source_db))
    throw std::invalid_argument("Audio monitor headroom preset is invalid");
  automatic_headroom_.store(settings.automatic_headroom, std::memory_order_release);
  attenuation_per_additional_source_db_.store(settings.attenuation_per_additional_source_db,
                                              std::memory_order_release);
  activate_audio_source_audition_bus_.store(settings.activate_audio_source_audition_bus,
                                            std::memory_order_release);
}

void LinuxAudioMonitor::publish_status(AudioMonitorStatus status)
{
  {
    std::lock_guard lock(status_mutex_);
    if (!status_changed(status_, status))
    {
      status_ = std::move(status);
      return;
    }
    status_ = status;
  }
  if (status_handler_)
    status_handler_(status);
}

void LinuxAudioMonitor::read_loop()
{
  while (!stopping_)
  {
    MappedAudioIpc ipc_mapping;
    if (!ipc_mapping.connect(ipc_names_))
    {
      publish_status({true, false, false, "backend-unavailable"});
      std::this_thread::sleep_for(reconnect_interval);
      continue;
    }

    const auto& mapping = ipc_mapping.mapping();
    auto& control = ipc_mapping.control();
    std::uint64_t last_sequence = 0;
    std::uint64_t last_heartbeat = 0;
    std::uint64_t heartbeat_seen_ns = 0;
    std::uint64_t last_frame_captured_ns = 0;
    std::uint64_t ipc_skipped_frames = 0;
    std::uint64_t ipc_sample_index_gaps = 0;
    std::uint64_t ipc_seqlock_retries = 0;
    std::uint64_t next_sample_index = 0;
    std::uint32_t last_topology_generation = 0;
    std::uint32_t producer_process_id = mapping.producer_process_id;
    if (!ipc::validate_audio_monitor_mapping(mapping))
      last_sequence = mapping.published_sequence.load(std::memory_order_acquire);

    while (!stopping_ && !ipc::validate_audio_monitor_mapping(mapping) &&
           ipc::compatible_audio_monitor_control(control) &&
           mapping.producer_process_id == producer_process_id)
    {
      control.automatic_headroom.store(automatic_headroom_.load(std::memory_order_acquire),
                                       std::memory_order_release);
      control.attenuation_per_additional_source_db.store(
          attenuation_per_additional_source_db_.load(std::memory_order_acquire),
          std::memory_order_release);
      control.activate_audio_source_audition_bus.store(
          activate_audio_source_audition_bus_.load(std::memory_order_acquire),
          std::memory_order_release);

      const auto now = monotonic_ns();
      const auto heartbeat = mapping.heartbeat_ns.load(std::memory_order_acquire);
      if (heartbeat != last_heartbeat)
      {
        last_heartbeat = heartbeat;
        heartbeat_seen_ns = now;
      }
      const bool fresh = ipc::audio_monitor_heartbeat_fresh(heartbeat_seen_ns, now);
      const bool configured = mapping.configured.load(std::memory_order_acquire) != 0;
      std::vector<SharedAudioMonitorFrame> copied_frames;
      if (fresh && configured)
      {
        const auto sequences = ipc::pending_audio_monitor_sequences(mapping, last_sequence);
        ipc_skipped_frames += sequences.skipped_frames;
        for (auto sequence = sequences.first_sequence; !sequences.empty(); ++sequence)
        {
          auto frame = ipc::copy_audio_monitor_frame(mapping, sequence);
          if (!frame)
          {
            ++ipc_seqlock_retries;
            break;
          }
          if (next_sample_index != 0 && frame->topology_generation == last_topology_generation &&
              frame->first_sample_index != next_sample_index)
          {
            ++ipc_sample_index_gaps;
          }
          last_sequence = sequence;
          last_frame_captured_ns = frame->captured_ns;
          next_sample_index = frame->first_sample_index + ipc::audio_monitor_frame_samples;
          last_topology_generation = frame->topology_generation;
          copied_frames.push_back(std::make_shared<AudioMonitorFrame>(std::move(*frame)));
          if (sequence == sequences.last_sequence)
            break;
        }
      }

      const auto observed_ns =
          last_frame_captured_ns != 0 ? last_frame_captured_ns : heartbeat_seen_ns;
      AudioMonitorStatus current;
      current.supported = true;
      current.available = fresh && configured;
      current.configured = configured;
      current.reason = !fresh       ? "backend-unavailable"
                       : configured ? std::string{}
                                    : "not-configured";
      current.sample_rate = mapping.sample_rate;
      current.channels = mapping.channels.load(std::memory_order_acquire);
      current.age_ms =
          observed_ns == 0 || observed_ns > now ? 0 : (now - observed_ns) / 1'000'000ULL;
      current.dropped_frames = mapping.dropped_frames.load(std::memory_order_relaxed);
      current.ipc_skipped_frames = ipc_skipped_frames;
      current.timestamp_discontinuities =
          mapping.timestamp_discontinuities.load(std::memory_order_relaxed);
      current.ingress_overflow_drops =
          mapping.ingress_overflow_drops.load(std::memory_order_relaxed);
      current.oversized_callback_drops =
          mapping.oversized_callback_drops.load(std::memory_order_relaxed);
      current.callback_count = mapping.callback_count.load(std::memory_order_relaxed);
      current.callback_frames_le_256 =
          mapping.callback_frames_le_256.load(std::memory_order_relaxed);
      current.callback_frames_le_512 =
          mapping.callback_frames_le_512.load(std::memory_order_relaxed);
      current.callback_frames_le_1024 =
          mapping.callback_frames_le_1024.load(std::memory_order_relaxed);
      current.callback_frames_le_2048 =
          mapping.callback_frames_le_2048.load(std::memory_order_relaxed);
      current.maximum_ingress_occupancy =
          mapping.maximum_ingress_occupancy.load(std::memory_order_relaxed);
      current.mixer_deadline_misses = mapping.mixer_deadline_misses.load(std::memory_order_relaxed);
      current.timeline_gaps = mapping.timeline_gaps.load(std::memory_order_relaxed);
      current.timeline_overlaps = mapping.timeline_overlaps.load(std::memory_order_relaxed);
      current.non_finite_samples = mapping.non_finite_samples.load(std::memory_order_relaxed);
      current.limiter_frames = mapping.limiter_frames.load(std::memory_order_relaxed);
      current.ipc_sample_index_gaps = ipc_sample_index_gaps;
      current.ipc_seqlock_retries = ipc_seqlock_retries;
      current.latest_peak =
          static_cast<double>(mapping.latest_peak_millionths.load(std::memory_order_relaxed)) /
          1'000'000.0;
      current.latest_rms =
          static_cast<double>(mapping.latest_rms_millionths.load(std::memory_order_relaxed)) /
          1'000'000.0;
      current.latest_dc =
          static_cast<double>(mapping.latest_dc_millionths.load(std::memory_order_relaxed)) /
          1'000'000.0;
      publish_status(current);
      if (frame_handler_)
        for (auto& frame : copied_frames)
          frame_handler_(std::move(frame));
      std::this_thread::sleep_for(polling_interval);
    }
  }
}

} // namespace studio_remote::linux
