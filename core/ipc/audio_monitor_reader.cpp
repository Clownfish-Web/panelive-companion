#include <studio_remote/ipc/audio_monitor_reader.hpp>

#include <studio_remote/ipc/audio_monitor_protocol.hpp>

#include <memory>
#include <string>
#include <utility>

namespace studio_remote::ipc
{
AudioMonitorReader::AudioMonitorReader(std::uint64_t initial_sequence)
    : last_sequence_(initial_sequence)
{
}

AudioMonitorRead AudioMonitorReader::read(const AudioMonitorMapping& mapping, std::uint64_t now_ns)
{
  const auto heartbeat = mapping.heartbeat_ns.load(std::memory_order_acquire);
  if (heartbeat != last_heartbeat_)
  {
    last_heartbeat_ = heartbeat;
    heartbeat_seen_ns_ = now_ns;
  }
  const bool fresh =
      heartbeat != 0 && ipc::audio_monitor_heartbeat_fresh(heartbeat_seen_ns_, now_ns);
  const bool configured = mapping.configured.load(std::memory_order_acquire) != 0;
  AudioMonitorRead result;
  if (fresh && configured)
    copy_pending_frames(mapping, result.frames);
  result.status = status(mapping, now_ns, fresh, configured);
  return result;
}

void AudioMonitorReader::copy_pending_frames(const AudioMonitorMapping& mapping,
                                             std::vector<SharedAudioMonitorFrame>& frames)
{
  const auto sequences = ipc::pending_audio_monitor_sequences(mapping, last_sequence_);
  ipc_skipped_frames_ += sequences.skipped_frames;
  // Retire the known overwritten range even if the first remaining slot still needs a retry.
  if (sequences.skipped_frames != 0)
    last_sequence_ = sequences.first_sequence - 1;
  for (auto sequence = sequences.first_sequence; !sequences.empty(); ++sequence)
  {
    auto frame = ipc::copy_audio_monitor_frame(mapping, sequence);
    if (!frame)
    {
      ++ipc_seqlock_retries_;
      break;
    }
    if (next_sample_index_ != 0 && frame->topology_generation == last_topology_generation_ &&
        frame->first_sample_index != next_sample_index_)
      ++ipc_sample_index_gaps_;
    last_sequence_ = sequence;
    last_frame_captured_ns_ = frame->captured_ns;
    next_sample_index_ = frame->first_sample_index + ipc::audio_monitor_frame_samples;
    last_topology_generation_ = frame->topology_generation;
    frames.push_back(std::make_shared<AudioMonitorFrame>(std::move(*frame)));
    if (sequence == sequences.last_sequence)
      break;
  }
}

AudioMonitorStatus AudioMonitorReader::status(const AudioMonitorMapping& mapping,
                                              std::uint64_t now,
                                              bool fresh,
                                              bool configured) const
{
  const auto observed_ns =
      last_frame_captured_ns_ != 0 ? last_frame_captured_ns_ : heartbeat_seen_ns_;
  AudioMonitorStatus current;
  current.supported = true;
  current.available = fresh && configured;
  current.configured = configured;
  current.reason = !fresh ? "backend-unavailable" : configured ? std::string{} : "not-configured";
  current.sample_rate = mapping.sample_rate;
  current.channels = mapping.channels.load(std::memory_order_acquire);
  current.age_ms = observed_ns == 0 || observed_ns > now ? 0 : (now - observed_ns) / 1'000'000ULL;
  current.dropped_frames = mapping.dropped_frames.load(std::memory_order_relaxed);
  current.ipc_skipped_frames = ipc_skipped_frames_;
  current.timestamp_discontinuities =
      mapping.timestamp_discontinuities.load(std::memory_order_relaxed);
  current.ingress_overflow_drops = mapping.ingress_overflow_drops.load(std::memory_order_relaxed);
  current.oversized_callback_drops =
      mapping.oversized_callback_drops.load(std::memory_order_relaxed);
  current.callback_count = mapping.callback_count.load(std::memory_order_relaxed);
  current.callback_frames_le_256 = mapping.callback_frames_le_256.load(std::memory_order_relaxed);
  current.callback_frames_le_512 = mapping.callback_frames_le_512.load(std::memory_order_relaxed);
  current.callback_frames_le_1024 = mapping.callback_frames_le_1024.load(std::memory_order_relaxed);
  current.callback_frames_le_2048 = mapping.callback_frames_le_2048.load(std::memory_order_relaxed);
  current.maximum_ingress_occupancy =
      mapping.maximum_ingress_occupancy.load(std::memory_order_relaxed);
  current.mixer_deadline_misses = mapping.mixer_deadline_misses.load(std::memory_order_relaxed);
  current.timeline_gaps = mapping.timeline_gaps.load(std::memory_order_relaxed);
  current.timeline_overlaps = mapping.timeline_overlaps.load(std::memory_order_relaxed);
  current.non_finite_samples = mapping.non_finite_samples.load(std::memory_order_relaxed);
  current.limiter_frames = mapping.limiter_frames.load(std::memory_order_relaxed);
  current.ipc_sample_index_gaps = ipc_sample_index_gaps_;
  current.ipc_seqlock_retries = ipc_seqlock_retries_;
  current.latest_peak =
      static_cast<double>(mapping.latest_peak_millionths.load(std::memory_order_relaxed)) /
      1'000'000.0;
  current.latest_rms =
      static_cast<double>(mapping.latest_rms_millionths.load(std::memory_order_relaxed)) /
      1'000'000.0;
  current.latest_dc =
      static_cast<double>(mapping.latest_dc_millionths.load(std::memory_order_relaxed)) /
      1'000'000.0;
  return current;
}
} // namespace studio_remote::ipc
