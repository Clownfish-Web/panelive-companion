#pragma once

#include "../../shared/capture/audio_monitor_worker.hpp"
#include "../ipc/macos_ipc_names.hpp"

#include <string>

namespace studio_remote::macos
{
/// Start/stop have one external lifecycle owner. Status/settings are safe from other threads.
/// Callbacks run on the reader outside locks, including its terminal unavailable notification
/// before stop joins it, and must not stop or destroy it. Callback targets and the optional
/// real-time service must outlive this worker and its thread registration.
class MacosAudioMonitor final
{
public:
  using FrameHandler = AudioMonitorWorker::FrameHandler;
  using StatusHandler = AudioMonitorWorker::StatusHandler;
  using MonotonicClock = AudioMonitorWorker::MonotonicClock;

  MacosAudioMonitor(FrameHandler frame_handler,
                    StatusHandler status_handler,
                    RealtimeAudioServices* realtime_audio = nullptr,
                    std::string audio_name = ipc_names::audio_monitor,
                    std::string control_name = ipc_names::audio_monitor_control,
                    MonotonicClock clock = {});
  ~MacosAudioMonitor();
  MacosAudioMonitor(const MacosAudioMonitor&) = delete;
  MacosAudioMonitor& operator=(const MacosAudioMonitor&) = delete;

  void start();
  void stop();
  AudioMonitorStatus status() const;
  void set_mix_settings(const AudioMonitorMixSettings& settings);

private:
  AudioMonitorWorker worker_;
};
} // namespace studio_remote::macos
