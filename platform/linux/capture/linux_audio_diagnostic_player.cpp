#include "linux_audio_diagnostic_player.hpp"

#include <studio_remote/common/types.hpp>
#include <studio_remote/ipc/audio_monitor_mapping.hpp>
#include <studio_remote/media/audio_diagnostic_gain.hpp>
#include <studio_remote/media/audio_diagnostic_peak_window.hpp>
#include <studio_remote/media/audio_diagnostic_session_log.hpp>

#include <pulse/error.h>
#include <pulse/simple.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace studio_remote::linux
{
namespace
{
constexpr std::size_t diagnostic_queue_capacity = 16;
constexpr std::size_t prebuffer_frame_count = 3;

std::runtime_error pulse_error(std::string_view operation, int error)
{
  return std::runtime_error(std::string(operation) + ": " + pa_strerror(error));
}
} // namespace

struct LinuxAudioDiagnosticPlayer::Implementation
{
  mutable std::mutex mutex;
  std::mutex lifecycle_mutex;
  std::condition_variable available;
  std::deque<SharedAudioMonitorFrame> frames;
  AudioDiagnosticPlaybackStatus playback_status;
  AudioMonitorStatus monitor_status;
  AudioDiagnosticPeakWindow peak_window;
  AudioDiagnosticSessionLog diagnostic_log;
  std::atomic<bool> stopping{false};
  std::atomic<float> gain_linear{1.0F};
  std::thread thread;
  float gain_db = 0.0F;
  std::uint64_t next_sample_index = 0;
  std::uint32_t topology_generation = 0;

  bool expire_diagnostic_session_locked(std::uint64_t observed_ns) noexcept
  {
    if (!diagnostic_log.expire_if_due(observed_ns))
      return false;
    playback_status.duration_limit_reached = true;
    stopping.store(true, std::memory_order_release);
    available.notify_all();
    return true;
  }

  std::vector<AudioOutputDevice> output_devices() const
  {
    return LinuxAudioOutputCatalog{}.output_devices();
  }

  void run(std::string device_id)
  {
    int error = 0;
    const pa_sample_spec format{PA_SAMPLE_FLOAT32LE, 48'000, 2};
    pa_simple* stream = pa_simple_new(nullptr,
                                      "PaneLive Companion",
                                      PA_STREAM_PLAYBACK,
                                      device_id.c_str(),
                                      "Local audio diagnostics",
                                      &format,
                                      nullptr,
                                      nullptr,
                                      &error);
    if (!stream)
    {
      std::lock_guard lock(mutex);
      playback_status.error = pulse_error("Opening the selected audio output", error).what();
      playback_status.playing = false;
      return;
    }

    bool started_playback = false;
    bool starved = false;
    while (!stopping.load(std::memory_order_acquire))
    {
      SharedAudioMonitorFrame frame;
      {
        std::unique_lock lock(mutex);
        expire_diagnostic_session_locked(monotonic_ns());
        available.wait_for(lock,
                           std::chrono::milliseconds(100),
                           [this, started_playback]
                           {
                             return stopping.load(std::memory_order_acquire) ||
                                    (!started_playback ? frames.size() >= prebuffer_frame_count
                                                       : !frames.empty());
                           });
        if (stopping.load(std::memory_order_acquire))
          break;
        if (frames.empty() || (!started_playback && frames.size() < prebuffer_frame_count))
        {
          playback_status.queued_frames = frames.size();
          if (started_playback && !starved)
          {
            ++playback_status.output_underruns;
            starved = true;
          }
          continue;
        }
        frame = std::move(frames.front());
        frames.pop_front();
        playback_status.queued_frames = frames.size();
        started_playback = true;
        starved = false;
      }

      std::vector<float> output(frame->samples.size() / frame->channels * 2);
      const auto gain = gain_linear.load(std::memory_order_relaxed);
      std::uint64_t clipped_samples = 0;
      for (std::size_t sample = 0; sample < output.size() / 2; ++sample)
      {
        const auto input = sample * frame->channels;
        const auto left = apply_audio_diagnostic_gain(frame->samples[input], gain);
        const auto right = apply_audio_diagnostic_gain(
            frame->channels == 1 ? frame->samples[input] : frame->samples[input + 1], gain);
        output[sample * 2] = left.value;
        output[sample * 2 + 1] = right.value;
        clipped_samples += left.clipped + right.clipped;
      }
      if (pa_simple_write(stream, output.data(), output.size() * sizeof(float), &error) < 0)
      {
        std::lock_guard lock(mutex);
        playback_status.error = pulse_error("Writing diagnostic audio", error).what();
        break;
      }
      std::lock_guard lock(mutex);
      playback_status.played_frames += output.size() / 2;
      playback_status.gain_clipped_samples += clipped_samples;
    }
    if (!stopping.load(std::memory_order_acquire))
      static_cast<void>(pa_simple_drain(stream, &error));
    pa_simple_free(stream);
    std::lock_guard lock(mutex);
    playback_status.playing = false;
    frames.clear();
    playback_status.queued_frames = 0;
  }
};

LinuxAudioDiagnosticPlayer::LinuxAudioDiagnosticPlayer()
    : implementation_(std::make_unique<Implementation>())
{
}

LinuxAudioDiagnosticPlayer::~LinuxAudioDiagnosticPlayer()
{
  stop();
}

std::vector<AudioOutputDevice> LinuxAudioDiagnosticPlayer::output_devices() const
{
  return implementation_->output_devices();
}

void LinuxAudioDiagnosticPlayer::start(std::string_view device_id, AudioDiagnosticTap tap)
{
  if (device_id.empty())
    throw std::invalid_argument("Select an audio output device");
  auto& state = *implementation_;
  std::lock_guard lifecycle_lock(state.lifecycle_mutex);
  state.stopping.store(true, std::memory_order_release);
  state.available.notify_all();
  if (state.thread.joinable())
    state.thread.join();

  const auto devices = state.output_devices();
  const auto selected =
      std::find_if(devices.begin(),
                   devices.end(),
                   [device_id](const auto& device) { return device.id == device_id; });
  if (selected == devices.end())
    throw std::invalid_argument("The selected audio output is no longer available");

  state.stopping.store(false, std::memory_order_release);
  {
    std::lock_guard lock(state.mutex);
    if (state.playback_status.playing)
      state.diagnostic_log.playback_stopped(monotonic_ns());
    state.frames.clear();
    state.playback_status = {};
    state.playback_status.playing = true;
    state.playback_status.tap = tap;
    state.playback_status.device_id = std::string(device_id);
    state.playback_status.device_name = selected->name;
    state.playback_status.gain_db = state.gain_db;
    state.next_sample_index = 0;
    state.topology_generation = 0;
    state.peak_window.reset();
    state.diagnostic_log.playback_started(tap == AudioDiagnosticTap::pre_opus_ipc
                                              ? AudioDiagnosticLogTap::pre_opus
                                              : AudioDiagnosticLogTap::post_opus,
                                          monotonic_ns());
  }
  state.thread = std::thread([implementation = &state, id = std::string(device_id)]
                             { implementation->run(id); });
}

void LinuxAudioDiagnosticPlayer::stop()
{
  auto& state = *implementation_;
  std::lock_guard lifecycle_lock(state.lifecycle_mutex);
  {
    std::lock_guard lock(state.mutex);
    if (state.playback_status.playing)
      state.diagnostic_log.playback_stopped(monotonic_ns());
  }
  state.stopping.store(true, std::memory_order_release);
  state.available.notify_all();
  if (state.thread.joinable())
    state.thread.join();
  std::lock_guard lock(state.mutex);
  state.frames.clear();
  state.peak_window.reset();
  state.playback_status.playing = false;
  state.playback_status.queued_frames = 0;
}

void LinuxAudioDiagnosticPlayer::set_gain_db(float gain_db)
{
  if (!valid_audio_diagnostic_gain(gain_db))
    throw std::invalid_argument("Diagnostic gain must be between -24 dB and +12 dB");
  auto& state = *implementation_;
  std::lock_guard lock(state.mutex);
  state.gain_db = gain_db;
  state.gain_linear.store(audio_diagnostic_gain_multiplier(gain_db), std::memory_order_relaxed);
  state.playback_status.gain_db = gain_db;
}

void LinuxAudioDiagnosticPlayer::set_monitor_status(const AudioMonitorStatus& status)
{
  std::lock_guard lock(implementation_->mutex);
  implementation_->monitor_status = status;
}

void LinuxAudioDiagnosticPlayer::push(SharedAudioMonitorFrame frame, AudioDiagnosticTap tap)
{
  if (!frame || frame->sample_rate != 48'000 || (frame->channels != 1 && frame->channels != 2))
    return;
  auto& state = *implementation_;
  std::lock_guard lock(state.mutex);
  if (!state.playback_status.playing || state.playback_status.tap != tap)
    return;
  const auto observed_ns = monotonic_ns();
  const auto frame_peak = state.peak_window.observe(frame->samples, observed_ns);
  ++state.playback_status.received_frames;
  const bool discontinuity =
      state.next_sample_index != 0 && (frame->topology_generation != state.topology_generation ||
                                       frame->first_sample_index != state.next_sample_index);
  if (discontinuity)
  {
    state.frames.clear();
    ++state.playback_status.discontinuities;
  }
  state.next_sample_index = frame->first_sample_index + ipc::audio_monitor_frame_samples;
  state.topology_generation = frame->topology_generation;
  if (state.frames.size() == diagnostic_queue_capacity)
  {
    state.frames.pop_front();
    ++state.playback_status.queue_overflows;
  }
  state.frames.push_back(frame);
  state.playback_status.queued_frames = state.frames.size();
  const auto peaks = state.peak_window.levels(observed_ns);

  AudioDiagnosticLogSnapshot snapshot;
  snapshot.tap = tap == AudioDiagnosticTap::pre_opus_ipc ? AudioDiagnosticLogTap::pre_opus
                                                         : AudioDiagnosticLogTap::post_opus;
  snapshot.observed_ns = observed_ns;
  snapshot.frame_sequence = frame->sequence;
  snapshot.first_sample_index = frame->first_sample_index;
  snapshot.captured_ns = frame->captured_ns;
  snapshot.topology_generation = frame->topology_generation;
  snapshot.frame_flags = frame->flags;
  snapshot.frame_peak = frame_peak;
  snapshot.maximum_peak_last_second = peaks.maximum;
  snapshot.average_peak_last_second = peaks.average;
  snapshot.peak_observations_last_second = peaks.observations;
  snapshot.received_frames = state.playback_status.received_frames;
  snapshot.played_frames = state.playback_status.played_frames;
  snapshot.queued_frames = state.frames.size();
  snapshot.queue_overflows = state.playback_status.queue_overflows;
  snapshot.output_underruns = state.playback_status.output_underruns;
  snapshot.discontinuities = state.playback_status.discontinuities;
  snapshot.silent_concealment_frames = state.playback_status.silent_concealment_frames;
  snapshot.gain_db = state.playback_status.gain_db;
  snapshot.gain_clipped_samples = state.playback_status.gain_clipped_samples;
  snapshot.monitor_available = state.monitor_status.available;
  snapshot.monitor_age_ms = state.monitor_status.age_ms;
  snapshot.callback_count = state.monitor_status.callback_count;
  snapshot.ingress_overflow_drops = state.monitor_status.ingress_overflow_drops;
  snapshot.oversized_callback_drops = state.monitor_status.oversized_callback_drops;
  snapshot.ipc_skipped_frames = state.monitor_status.ipc_skipped_frames;
  snapshot.timestamp_discontinuities = state.monitor_status.timestamp_discontinuities;
  snapshot.timeline_gaps = state.monitor_status.timeline_gaps;
  snapshot.timeline_overlaps = state.monitor_status.timeline_overlaps;
  snapshot.monitor_latest_peak = state.monitor_status.latest_peak;
  snapshot.monitor_latest_rms = state.monitor_status.latest_rms;
  state.diagnostic_log.record(snapshot);
  state.available.notify_one();
}

AudioDiagnosticPlaybackStatus LinuxAudioDiagnosticPlayer::status() const
{
  auto& state = *implementation_;
  std::lock_guard lock(state.mutex);
  auto result = state.playback_status;
  result.queued_frames = state.frames.size();
  const auto peaks = state.peak_window.levels(monotonic_ns());
  result.maximum_peak_last_second = peaks.maximum;
  result.average_peak_last_second = peaks.average;
  result.peak_observations_last_second = peaks.observations;
  return result;
}

void LinuxAudioDiagnosticPlayer::begin_debug_log(std::string_view companion_version,
                                                 std::string_view obs_version)
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto unix_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
  auto& state = *implementation_;
  std::lock_guard lock(state.mutex);
  state.diagnostic_log.begin(companion_version, obs_version, unix_ms, monotonic_ns());
  state.playback_status.duration_limit_reached = false;
  if (state.playback_status.playing)
    state.diagnostic_log.playback_started(state.playback_status.tap ==
                                                  AudioDiagnosticTap::pre_opus_ipc
                                              ? AudioDiagnosticLogTap::pre_opus
                                              : AudioDiagnosticLogTap::post_opus,
                                          monotonic_ns());
}

void LinuxAudioDiagnosticPlayer::complete_debug_log()
{
  std::lock_guard lock(implementation_->mutex);
  implementation_->diagnostic_log.complete(monotonic_ns());
}

bool LinuxAudioDiagnosticPlayer::debug_log_active() const
{
  auto& state = *implementation_;
  std::lock_guard lock(state.mutex);
  state.expire_diagnostic_session_locked(monotonic_ns());
  return state.diagnostic_log.active();
}

bool LinuxAudioDiagnosticPlayer::debug_log_available() const
{
  std::lock_guard lock(implementation_->mutex);
  return implementation_->diagnostic_log.report_available();
}

std::string LinuxAudioDiagnosticPlayer::debug_log_report() const
{
  std::lock_guard lock(implementation_->mutex);
  return implementation_->diagnostic_log.report();
}

} // namespace studio_remote::linux
