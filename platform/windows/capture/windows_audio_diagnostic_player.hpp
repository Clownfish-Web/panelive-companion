#pragma once

#include <studio_remote/media/audio_monitor.hpp>
#include <studio_remote/media/realtime_audio_services.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote::windows
{

enum class AudioDiagnosticTap
{
  pre_opus_ipc,
  post_opus
};

struct AudioOutputDevice
{
  std::string id;
  std::string name;
  bool is_default = false;
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
  std::uint64_t degraded_clock_reads = 0;
  std::uint64_t endpoint_clock_observations = 0;
  std::uint64_t rate_adjustments = 0;
  std::uint64_t rate_adjustment_failures = 0;
  std::uint64_t rate_adjustment_saturations = 0;
  std::uint64_t device_recovery_attempts = 0;
  std::uint64_t device_recoveries = 0;
  std::uint32_t engine_buffer_frames = 0;
  std::uint32_t endpoint_mix_sample_rate = 0;
  std::uint16_t endpoint_mix_channels = 0;
  std::uint32_t padding_low_watermark = 0;
  std::uint32_t padding_high_watermark = 0;
  std::size_t playout_low_watermark_frames = 0;
  std::size_t playout_target_frames = 0;
  std::size_t playout_high_watermark_frames = 0;
  double rate_adjustment_ppm = 0.0;
  bool prebuffering = true;
  std::size_t queued_frames = 0;
  float gain_db = 0.0F;
  std::uint64_t gain_clipped_samples = 0;
  float maximum_peak_last_second = 0.0F;
  float average_peak_last_second = 0.0F;
  std::size_t peak_observations_last_second = 0;
  bool duration_limit_reached = false;
};

// Local-only WASAPI playback of one selected monitor diagnostic tap. Operating-system device
// identities never cross into the network API.
class WindowsAudioDiagnosticPlayer
{
public:
  explicit WindowsAudioDiagnosticPlayer(RealtimeAudioServices* realtime_audio = nullptr);
  ~WindowsAudioDiagnosticPlayer();

  WindowsAudioDiagnosticPlayer(const WindowsAudioDiagnosticPlayer&) = delete;
  WindowsAudioDiagnosticPlayer& operator=(const WindowsAudioDiagnosticPlayer&) = delete;

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
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace studio_remote::windows
