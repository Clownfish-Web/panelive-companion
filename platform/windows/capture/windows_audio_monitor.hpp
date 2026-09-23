#pragma once

#include "../../shared/capture/audio_monitor_worker.hpp"

namespace studio_remote::windows
{

/// One external owner serializes start/stop. Callbacks run on the reader outside status locks,
/// including its terminal unavailable notification before stop joins it. Callback targets and
/// realtime_audio must outlive the worker; callbacks must not stop or destroy their own worker.
class WindowsAudioMonitor
{
public:
  using FrameHandler = AudioMonitorWorker::FrameHandler;
  using StatusHandler = AudioMonitorWorker::StatusHandler;

  WindowsAudioMonitor(FrameHandler frame_handler,
                      StatusHandler status_handler,
                      RealtimeAudioServices* realtime_audio = nullptr);
  ~WindowsAudioMonitor();

  WindowsAudioMonitor(const WindowsAudioMonitor&) = delete;
  WindowsAudioMonitor& operator=(const WindowsAudioMonitor&) = delete;

  void start();
  void stop();
  AudioMonitorStatus status() const;
  void set_mix_settings(const AudioMonitorMixSettings& settings);

private:
  AudioMonitorWorker worker_;
};
} // namespace studio_remote::windows
