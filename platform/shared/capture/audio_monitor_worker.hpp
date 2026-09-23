#pragma once

#include <studio_remote/media/audio_monitor_connection.hpp>
#include <studio_remote/media/realtime_audio_services.hpp>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace studio_remote
{
/// Start/stop have one external owner. Callbacks run on the reader outside locks, including its
/// terminal notification before join; callbacks cannot stop/destroy the worker. Callback targets
/// and realtime services must outlive it. Status/settings may be accessed from other threads.
class AudioMonitorWorker final
{
public:
  using ConnectionFactory = std::function<std::unique_ptr<IAudioMonitorConnection>()>;
  using MonotonicClock = std::function<std::uint64_t()>;
  using FrameHandler = std::function<void(SharedAudioMonitorFrame)>;
  using StatusHandler = std::function<void(const AudioMonitorStatus&)>;
  using FailureReporter = void (*)(const char* message) noexcept;

  AudioMonitorWorker(ConnectionFactory connections,
                     MonotonicClock clock,
                     FrameHandler frame_handler,
                     StatusHandler status_handler,
                     RealtimeAudioServices* realtime_audio,
                     FailureReporter report_failure);
  ~AudioMonitorWorker();
  AudioMonitorWorker(const AudioMonitorWorker&) = delete;
  AudioMonitorWorker& operator=(const AudioMonitorWorker&) = delete;

  void start();
  void stop();
  AudioMonitorStatus status() const;
  void set_mix_settings(const AudioMonitorMixSettings& settings);

private:
  void read_loop(std::stop_token stop);
  bool poll(std::unique_ptr<IAudioMonitorConnection>& resources);
  void publish_status(AudioMonitorStatus status);
  void publish_stopped_status() noexcept;
  void store_unavailable();

  const ConnectionFactory connections_;
  const MonotonicClock clock_;
  const FrameHandler frame_handler_;
  const StatusHandler status_handler_;
  RealtimeAudioServices* const realtime_audio_;
  const FailureReporter report_failure_;
  mutable std::mutex state_mutex_;
  AudioMonitorStatus status_{true, false, false, "backend-unavailable"};
  AudioMonitorMixSettings settings_;
  std::mutex wait_mutex_;
  std::condition_variable_any wake_;
  // Synchronize callback self-stop checks with start and the external owner's join bookkeeping.
  std::mutex lifecycle_mutex_;
  std::thread::id reader_thread_id_;
  std::jthread thread_;
};
} // namespace studio_remote
