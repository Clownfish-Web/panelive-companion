#pragma once

#include "../../shared/capture/audio_monitor_worker.hpp"
#include "linux_audio_monitor_ipc.hpp"

namespace studio_remote::linux
{

/// One external owner serializes start/stop. Callbacks run on the reader outside status locks,
/// including its terminal unavailable notification before stop joins it. Callback targets must
/// outlive the worker; callbacks must not stop or destroy their own worker.
class LinuxAudioMonitor
{
public:
  using FrameHandler = AudioMonitorWorker::FrameHandler;
  using StatusHandler = AudioMonitorWorker::StatusHandler;

  LinuxAudioMonitor(FrameHandler frame_handler,
                    StatusHandler status_handler,
                    LinuxAudioMonitorIpcNames ipc_names = {});
  ~LinuxAudioMonitor();

  LinuxAudioMonitor(const LinuxAudioMonitor&) = delete;
  LinuxAudioMonitor& operator=(const LinuxAudioMonitor&) = delete;

  void start();
  void stop();
  AudioMonitorStatus status() const;
  void set_mix_settings(const AudioMonitorMixSettings& settings);

private:
  AudioMonitorWorker worker_;
};
} // namespace studio_remote::linux
