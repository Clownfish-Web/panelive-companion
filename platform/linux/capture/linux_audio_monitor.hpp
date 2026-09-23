#pragma once

#include <studio_remote/ipc/audio_monitor_mapping.hpp>
#include <studio_remote/media/audio_monitor.hpp>
#include <studio_remote/media/audio_monitor_mix_settings.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace studio_remote::linux
{

struct LinuxAudioMonitorIpcNames
{
  std::string audio = ipc::audio_monitor_shared_memory_name;
  std::string control = ipc::audio_monitor_control_shared_memory_name;
};

class LinuxAudioMonitor
{
public:
  using FrameHandler = std::function<void(SharedAudioMonitorFrame)>;
  using StatusHandler = std::function<void(const AudioMonitorStatus&)>;

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
  void read_loop();
  void publish_status(AudioMonitorStatus status);

  FrameHandler frame_handler_;
  StatusHandler status_handler_;
  LinuxAudioMonitorIpcNames ipc_names_;
  mutable std::mutex status_mutex_;
  AudioMonitorStatus status_{true, false, false, "backend-unavailable"};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> automatic_headroom_{true};
  std::atomic<std::uint32_t> attenuation_per_additional_source_db_{
      default_audio_monitor_headroom_db};
  std::atomic<bool> activate_audio_source_audition_bus_{false};
  std::thread thread_;
};

} // namespace studio_remote::linux
