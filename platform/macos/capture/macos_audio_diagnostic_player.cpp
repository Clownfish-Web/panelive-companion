#include "macos_audio_diagnostic_state.hpp"

#include <studio_remote/common/types.hpp>
#include <studio_remote/ipc/audio_monitor_mapping.hpp>
#include <studio_remote/media/audio_diagnostic_gain.hpp>

#include <algorithm>
#include <stdexcept>

namespace studio_remote::macos
{
MacosAudioDiagnosticPlayer::MacosAudioDiagnosticPlayer(RealtimeAudioServices* services)
    : impl_(std::make_unique<Impl>(services))
{
}
MacosAudioDiagnosticPlayer::~MacosAudioDiagnosticPlayer()
{
  stop();
}

std::vector<AudioOutputDevice> MacosAudioDiagnosticPlayer::output_devices() const
{
  return MacosAudioOutputCatalog{}.output_devices();
}
void MacosAudioDiagnosticPlayer::Impl::stop_worker()
{
  thread.request_stop();
  wake.notify_all();
  if (thread.joinable())
    thread.join();
}
void MacosAudioDiagnosticPlayer::start(std::string_view device_id, AudioDiagnosticTap tap)
{
  if (device_id.empty())
    throw std::invalid_argument("Select an audio output device");
  std::lock_guard lifecycle(impl_->lifecycle_mutex);
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->status.playing)
      impl_->diagnostic_log.playback_stopped(monotonic_ns());
  }
  impl_->stop_worker();
  const auto devices = output_devices();
  const auto selected =
      std::find_if(devices.begin(),
                   devices.end(),
                   [device_id](const auto& device) { return device.id == device_id; });
  {
    std::lock_guard lock(impl_->mutex);
    impl_->frames.clear();
    impl_->status = {};
    impl_->status.playing = true;
    impl_->status.tap = tap;
    impl_->status.gain_db = impl_->gain_db;
    impl_->status.device_id = device_id;
    if (selected != devices.end())
      impl_->status.device_name = selected->name;
    impl_->next_sample_index = 0;
    impl_->topology_generation = 0;
    ++impl_->queue_generation;
    impl_->playout.reset();
    impl_->peak_window.reset();
    impl_->diagnostic_log.playback_started(tap == AudioDiagnosticTap::pre_opus_ipc
                                               ? AudioDiagnosticLogTap::pre_opus
                                               : AudioDiagnosticLogTap::post_opus,
                                           monotonic_ns());
  }
  impl_->thread = std::jthread([state = impl_.get(), id = std::string(device_id)](
                                   std::stop_token stop) { state->run(stop, id); });
}
void MacosAudioDiagnosticPlayer::stop()
{
  std::lock_guard lifecycle(impl_->lifecycle_mutex);
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->status.playing)
      impl_->diagnostic_log.playback_stopped(monotonic_ns());
  }
  impl_->stop_worker();
  std::lock_guard lock(impl_->mutex);
  impl_->frames.clear();
  impl_->peak_window.reset();
  impl_->status.playing = false;
}

bool MacosAudioDiagnosticPlayer::Impl::expire_locked(std::uint64_t now)
{
  if (diagnostic_log.expire_if_due(now))
  {
    status.duration_limit_reached = true;
    wake.notify_all();
  }
  return status.duration_limit_reached;
}
void MacosAudioDiagnosticPlayer::Impl::record_frame_locked(const AudioMonitorFrame& frame,
                                                           AudioDiagnosticTap tap,
                                                           float peak_value,
                                                           std::uint64_t observed_ns)
{
  const auto peaks = peak_window.levels(observed_ns);
  AudioDiagnosticLogSnapshot snapshot;
  snapshot.tap = tap == AudioDiagnosticTap::pre_opus_ipc ? AudioDiagnosticLogTap::pre_opus
                                                         : AudioDiagnosticLogTap::post_opus;
  snapshot.observed_ns = observed_ns;
  snapshot.frame_sequence = frame.sequence;
  snapshot.first_sample_index = frame.first_sample_index;
  snapshot.captured_ns = frame.captured_ns;
  snapshot.topology_generation = frame.topology_generation;
  snapshot.frame_flags = frame.flags;
  snapshot.frame_peak = peak_value;
  snapshot.maximum_peak_last_second = peaks.maximum;
  snapshot.average_peak_last_second = peaks.average;
  snapshot.peak_observations_last_second = peaks.observations;
  snapshot.received_frames = status.received_frames;
  snapshot.played_frames = status.played_frames;
  snapshot.queued_frames = frames.size();
  snapshot.queue_overflows = status.queue_overflows;
  snapshot.output_underruns = status.output_underruns;
  snapshot.discontinuities = status.discontinuities;
  snapshot.silent_concealment_frames = status.silent_concealment_frames;
  snapshot.prebuffering = status.prebuffering;
  snapshot.gain_db = status.gain_db;
  snapshot.gain_clipped_samples = status.gain_clipped_samples;
  snapshot.monitor_available = monitor_status.available;
  snapshot.monitor_age_ms = monitor_status.age_ms;
  snapshot.callback_count = monitor_status.callback_count;
  snapshot.ingress_overflow_drops = monitor_status.ingress_overflow_drops;
  snapshot.oversized_callback_drops = monitor_status.oversized_callback_drops;
  snapshot.ipc_skipped_frames = monitor_status.ipc_skipped_frames;
  snapshot.timestamp_discontinuities = monitor_status.timestamp_discontinuities;
  snapshot.timeline_gaps = monitor_status.timeline_gaps;
  snapshot.timeline_overlaps = monitor_status.timeline_overlaps;
  snapshot.monitor_latest_peak = monitor_status.latest_peak;
  snapshot.monitor_latest_rms = monitor_status.latest_rms;
  diagnostic_log.record(snapshot);
}
void MacosAudioDiagnosticPlayer::set_gain_db(float gain_db)
{
  if (!valid_audio_diagnostic_gain(gain_db))
    throw std::invalid_argument("Diagnostic gain must be between -24 dB and +12 dB");
  std::lock_guard lock(impl_->mutex);
  impl_->gain_db = gain_db;
  impl_->status.gain_db = gain_db;
}

void MacosAudioDiagnosticPlayer::set_monitor_status(const AudioMonitorStatus& status)
{
  std::lock_guard lock(impl_->mutex);
  impl_->monitor_status = status;
}

void MacosAudioDiagnosticPlayer::push(SharedAudioMonitorFrame frame, AudioDiagnosticTap tap)
{
  if (!frame || frame->sample_rate != 48'000 || (frame->channels != 1 && frame->channels != 2) ||
      frame->samples.size() !=
          static_cast<std::size_t>(ipc::audio_monitor_frame_samples) * frame->channels)
    return;
  if (!std::all_of(frame->samples.begin(),
                   frame->samples.end(),
                   [](float sample) { return std::isfinite(sample); }))
    return;
  std::lock_guard lock(impl_->mutex);
  if (!impl_->status.playing || impl_->status.tap != tap)
    return;
  const auto observed_ns = monotonic_ns();
  const auto frame_peak = impl_->peak_window.observe(frame->samples, observed_ns);
  ++impl_->status.received_frames;
  const bool discontinuity =
      impl_->next_sample_index != 0 && (frame->topology_generation != impl_->topology_generation ||
                                        frame->first_sample_index != impl_->next_sample_index);
  if (discontinuity)
  {
    impl_->frames.clear();
    ++impl_->queue_generation;
    ++impl_->status.discontinuities;
    impl_->status.prebuffering = true;
    impl_->playout.reset();
  }
  impl_->next_sample_index = frame->first_sample_index + ipc::audio_monitor_frame_samples;
  impl_->topology_generation = frame->topology_generation;
  if (impl_->playout.above_high_watermark(impl_->frames.size() + 1) || impl_->frames.size() == 16)
  {
    impl_->frames.pop_front();
    ++impl_->queue_generation;
    ++impl_->status.queue_overflows;
    impl_->status.prebuffering = true;
  }
  impl_->frames.push_back(frame);
  impl_->record_frame_locked(*frame, tap, frame_peak, observed_ns);
}
AudioDiagnosticPlaybackStatus MacosAudioDiagnosticPlayer::status() const
{
  std::lock_guard lock(impl_->mutex);
  auto result = impl_->status;
  result.queued_frames = impl_->frames.size();
  const auto peaks = impl_->peak_window.levels(monotonic_ns());
  result.maximum_peak_last_second = peaks.maximum;
  result.average_peak_last_second = peaks.average;
  result.peak_observations_last_second = peaks.observations;
  return result;
}

void MacosAudioDiagnosticPlayer::begin_debug_log(std::string_view companion_version,
                                                 std::string_view obs_version)
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto unix_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
  std::lock_guard lock(impl_->mutex);
  impl_->diagnostic_log.begin(companion_version, obs_version, unix_ms, monotonic_ns());
  impl_->status.duration_limit_reached = false;
  if (impl_->status.playing)
  {
    impl_->diagnostic_log.playback_started(impl_->status.tap == AudioDiagnosticTap::pre_opus_ipc
                                               ? AudioDiagnosticLogTap::pre_opus
                                               : AudioDiagnosticLogTap::post_opus,
                                           monotonic_ns());
  }
}

void MacosAudioDiagnosticPlayer::complete_debug_log()
{
  std::lock_guard lock(impl_->mutex);
  impl_->diagnostic_log.complete(monotonic_ns());
}

bool MacosAudioDiagnosticPlayer::debug_log_active() const
{
  std::lock_guard lock(impl_->mutex);
  impl_->expire_locked(monotonic_ns());
  return impl_->diagnostic_log.active();
}

bool MacosAudioDiagnosticPlayer::debug_log_available() const
{
  std::lock_guard lock(impl_->mutex);
  return impl_->diagnostic_log.report_available();
}

std::string MacosAudioDiagnosticPlayer::debug_log_report() const
{
  std::lock_guard lock(impl_->mutex);
  return impl_->diagnostic_log.report();
}

} // namespace studio_remote::macos
