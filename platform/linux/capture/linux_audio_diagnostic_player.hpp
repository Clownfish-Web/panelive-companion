#pragma once

#include "linux_audio_output_catalog.hpp"

#include <studio_remote/media/audio_monitor.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote::linux
{

enum class AudioDiagnosticTap
{
  pre_opus_ipc,
  post_opus
};

struct AudioDiagnosticPlaybackStatus
{
  bool playing = false;
  AudioDiagnosticTap tap = AudioDiagnosticTap::pre_opus_ipc;
  std::string device_id;
  std::string device_name;
  std::string error;
  std::uint64_t received_frames = 0;
  std::uint64_t played_frames = 0;
  std::uint64_t queue_overflows = 0;
  std::uint64_t output_underruns = 0;
  std::uint64_t discontinuities = 0;
  std::uint64_t silent_concealment_frames = 0;
  std::size_t queued_frames = 0;
  float gain_db = 0.0F;
  std::uint64_t gain_clipped_samples = 0;
  float maximum_peak_last_second = 0.0F;
  float average_peak_last_second = 0.0F;
  std::size_t peak_observations_last_second = 0;
  bool duration_limit_reached = false;
};

// Local-only playback of one selected monitor diagnostic tap through the user's PulseAudio API.
// Device identities remain local and never enter the network API.
class LinuxAudioDiagnosticPlayer
{
public:
  LinuxAudioDiagnosticPlayer();
  ~LinuxAudioDiagnosticPlayer();

  LinuxAudioDiagnosticPlayer(const LinuxAudioDiagnosticPlayer&) = delete;
  LinuxAudioDiagnosticPlayer& operator=(const LinuxAudioDiagnosticPlayer&) = delete;

  std::vector<AudioOutputDevice> output_devices() const;
  void start(std::string_view device_id, AudioDiagnosticTap tap);
  void stop();
  void set_gain_db(float gain_db);
  void set_monitor_status(const AudioMonitorStatus& status);
  void push(SharedAudioMonitorFrame frame, AudioDiagnosticTap tap);
  AudioDiagnosticPlaybackStatus status() const;
  void begin_debug_log(std::string_view companion_version, std::string_view obs_version);
  void complete_debug_log();
  bool debug_log_active() const;
  bool debug_log_available() const;
  std::string debug_log_report() const;

private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace studio_remote::linux
