#include "audio_monitor_worker.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace studio_remote
{
AudioMonitorWorker::AudioMonitorWorker(ConnectionFactory connections,
                                       MonotonicClock clock,
                                       FrameHandler frame_handler,
                                       StatusHandler status_handler,
                                       RealtimeAudioServices* realtime_audio,
                                       FailureReporter report_failure)
    : connections_(std::move(connections)), clock_(std::move(clock)),
      frame_handler_(std::move(frame_handler)), status_handler_(std::move(status_handler)),
      realtime_audio_(realtime_audio), report_failure_(report_failure)
{
  if (!connections_ || !clock_ || !report_failure_)
    throw std::invalid_argument("Audio monitor worker requires IPC, clock and error reporting");
}
AudioMonitorWorker::~AudioMonitorWorker()
{
  stop();
}
void AudioMonitorWorker::start()
{
  std::lock_guard lock(lifecycle_mutex_);
  if (thread_.joinable())
    return;
  thread_ = std::jthread(
      [this](std::stop_token stop) noexcept
      {
        try
        {
          read_loop(stop);
        }
        catch (...)
        {
          // Contain failures outside polling too, such as a native condition-variable error.
          report_failure_("OBS audio monitor reader terminated unexpectedly");
          publish_stopped_status();
        }
      });
  reader_thread_id_ = thread_.get_id();
}
void AudioMonitorWorker::stop()
{
  std::jthread stopping;
  {
    std::lock_guard lock(lifecycle_mutex_);
    if (reader_thread_id_ == std::this_thread::get_id())
      throw std::logic_error("Audio callbacks cannot stop their worker");
    stopping = std::move(thread_);
  }
  if (stopping.joinable())
  {
    stopping.request_stop();
    wake_.notify_all();
    stopping.join();
    std::lock_guard lock(lifecycle_mutex_);
    reader_thread_id_ = {};
  }
}
AudioMonitorStatus AudioMonitorWorker::status() const
{
  std::lock_guard lock(state_mutex_);
  return status_;
}
void AudioMonitorWorker::set_mix_settings(const AudioMonitorMixSettings& settings)
{
  if (!valid_audio_monitor_headroom_db(settings.attenuation_per_additional_source_db))
    throw std::invalid_argument("Audio monitor headroom preset is invalid");
  std::lock_guard lock(state_mutex_);
  settings_ = settings;
}
void AudioMonitorWorker::store_unavailable()
{
  std::lock_guard lock(state_mutex_);
  status_ = {true, false, false, "backend-unavailable"};
}

void AudioMonitorWorker::publish_status(AudioMonitorStatus status)
{
  {
    std::lock_guard lock(state_mutex_);
    if (status.available == status_.available && status.configured == status_.configured &&
        status.reason == status_.reason && status.sample_rate == status_.sample_rate &&
        status.channels == status_.channels && status.dropped_frames == status_.dropped_frames &&
        status.ipc_skipped_frames == status_.ipc_skipped_frames &&
        status.timestamp_discontinuities == status_.timestamp_discontinuities &&
        status.ingress_overflow_drops == status_.ingress_overflow_drops &&
        status.oversized_callback_drops == status_.oversized_callback_drops &&
        status.callback_count == status_.callback_count &&
        status.maximum_ingress_occupancy == status_.maximum_ingress_occupancy &&
        status.mixer_deadline_misses == status_.mixer_deadline_misses &&
        status.timeline_gaps == status_.timeline_gaps &&
        status.timeline_overlaps == status_.timeline_overlaps &&
        status.non_finite_samples == status_.non_finite_samples &&
        status.limiter_frames == status_.limiter_frames &&
        status.ipc_sample_index_gaps == status_.ipc_sample_index_gaps &&
        status.ipc_seqlock_retries == status_.ipc_seqlock_retries &&
        status.latest_peak == status_.latest_peak && status.latest_rms == status_.latest_rms &&
        status.latest_dc == status_.latest_dc)
    {
      status_ = std::move(status);
      return;
    }
    status_ = status;
  }
  if (status_handler_)
    status_handler_(status);
}

void AudioMonitorWorker::publish_stopped_status() noexcept
{
  try
  {
    const AudioMonitorStatus stopped{true, false, false, "backend-unavailable"};
    {
      std::lock_guard lock(state_mutex_);
      status_ = stopped;
    }
    // Always close the reader's notification stream, even when an earlier callback failed after
    // updating only some consumers. This runs once per started reader, never on repeated stop.
    if (status_handler_)
      status_handler_(stopped);
  }
  catch (...)
  {
    report_failure_("OBS audio monitor stopped-status notification failed");
  }
}

bool AudioMonitorWorker::poll(std::unique_ptr<IAudioMonitorConnection>& resources)
{
  if (!resources)
  {
    auto opened = connections_();
    if (!opened)
    {
      publish_status({true, false, false, "backend-unavailable"});
      return false;
    }
    resources = std::move(opened);
  }
  AudioMonitorMixSettings settings;
  {
    std::lock_guard lock(state_mutex_);
    settings = settings_;
  }
  if (!resources->publish_settings(settings))
  {
    resources.reset();
    publish_status({true, false, false, "backend-unavailable"});
    return false;
  }
  auto observed = resources->read(clock_());
  if (!observed)
  {
    resources.reset();
    publish_status({true, false, false, "backend-unavailable"});
    return false;
  }
  // Release stale producer resources so the next attempt acquires the current pair.
  const bool fresh = observed->status.reason != "backend-unavailable";
  if (!fresh)
    resources.reset();
  publish_status(std::move(observed->status));
  if (frame_handler_)
    for (auto& frame : observed->frames)
      frame_handler_(std::move(frame));
  return fresh;
}

void AudioMonitorWorker::read_loop(std::stop_token stop)
{
  auto registration = realtime_audio_
                          ? realtime_audio_->register_current_thread(RealtimeAudioRole::ipc_reader)
                          : nullptr;
  std::unique_ptr<IAudioMonitorConnection> resources;
  bool failure_logged = false;
  while (!stop.stop_requested())
  {
    bool connected = false;
    try
    {
      connected = poll(resources);
    }
    catch (...)
    {
      resources.reset();
      store_unavailable();
      if (!failure_logged)
      {
        report_failure_("OBS audio monitor worker failed; reconnecting helper resources");
        failure_logged = true;
      }
    }
    std::unique_lock wait_lock(wait_mutex_);
    wake_.wait_for(wait_lock,
                   stop,
                   connected ? std::chrono::milliseconds(5) : std::chrono::milliseconds(250),
                   [] { return false; });
  }
  resources.reset();
  publish_stopped_status();
}
} // namespace studio_remote
