#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace studio_remote
{

inline constexpr std::size_t audio_diagnostic_log_capacity = 4'096;
inline constexpr std::uint64_t audio_diagnostic_log_duration_limit_ns =
    5ULL * 60ULL * 1'000'000'000ULL;

enum class AudioDiagnosticLogTap : std::uint8_t
{
  pre_opus,
  post_opus
};

struct AudioDiagnosticLogSnapshot
{
  AudioDiagnosticLogTap tap = AudioDiagnosticLogTap::pre_opus;
  std::uint64_t observed_ns = 0;
  std::uint64_t frame_sequence = 0;
  std::uint64_t first_sample_index = 0;
  std::uint64_t captured_ns = 0;
  std::uint32_t topology_generation = 0;
  std::uint16_t frame_flags = 0;
  float frame_peak = 0.0F;
  float maximum_peak_last_second = 0.0F;
  float average_peak_last_second = 0.0F;
  std::uint64_t peak_observations_last_second = 0;
  std::uint64_t received_frames = 0;
  std::uint64_t played_frames = 0;
  std::uint64_t queued_frames = 0;
  std::uint64_t queue_overflows = 0;
  std::uint64_t output_underruns = 0;
  std::uint64_t discontinuities = 0;
  std::uint64_t silent_concealment_frames = 0;
  bool prebuffering = true;
  float gain_db = 0.0F;
  std::uint64_t gain_clipped_samples = 0;
  bool monitor_available = false;
  std::uint64_t monitor_age_ms = 0;
  std::uint64_t callback_count = 0;
  std::uint64_t ingress_overflow_drops = 0;
  std::uint64_t oversized_callback_drops = 0;
  std::uint64_t ipc_skipped_frames = 0;
  std::uint64_t timestamp_discontinuities = 0;
  std::uint64_t timeline_gaps = 0;
  std::uint64_t timeline_overlaps = 0;
  double monitor_latest_peak = 0.0;
  double monitor_latest_rms = 0.0;
};

// Fixed-capacity scalar recorder for a user-initiated diagnostic session. It never owns PCM,
// credentials, device identifiers, source names, or network addresses.
class AudioDiagnosticSessionLog
{
public:
  void begin(std::string_view companion_version,
             std::string_view obs_version,
             std::uint64_t started_unix_ms,
             std::uint64_t started_monotonic_ns);
  void playback_started(AudioDiagnosticLogTap tap, std::uint64_t observed_ns) noexcept;
  void playback_stopped(std::uint64_t observed_ns) noexcept;
  void record(AudioDiagnosticLogSnapshot snapshot) noexcept;
  void complete(std::uint64_t observed_ns) noexcept;
  bool expire_if_due(std::uint64_t observed_ns) noexcept;

  bool active() const noexcept;
  bool report_available() const noexcept;
  bool completed_due_to_duration_limit() const noexcept;
  std::string report() const;

private:
  enum class RecordKind : std::uint8_t
  {
    playback_started,
    playback_stopped,
    snapshot
  };

  struct Record
  {
    RecordKind kind = RecordKind::snapshot;
    AudioDiagnosticLogTap tap = AudioDiagnosticLogTap::pre_opus;
    std::uint64_t observed_ns = 0;
    AudioDiagnosticLogSnapshot snapshot;
  };

  void append(Record record) noexcept;

  std::string companion_version_;
  std::string obs_version_;
  std::uint64_t started_unix_ms_ = 0;
  std::uint64_t started_monotonic_ns_ = 0;
  std::uint64_t completed_monotonic_ns_ = 0;
  Record records_[audio_diagnostic_log_capacity]{};
  std::size_t first_ = 0;
  std::size_t size_ = 0;
  std::uint64_t dropped_records_ = 0;
  bool active_ = false;
  bool complete_ = false;
  bool completed_due_to_duration_limit_ = false;
};

} // namespace studio_remote
