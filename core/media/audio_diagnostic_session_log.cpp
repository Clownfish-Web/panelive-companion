#include <studio_remote/media/audio_diagnostic_session_log.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>

namespace studio_remote
{
namespace
{
using json = nlohmann::json;

std::string_view tap_name(AudioDiagnosticLogTap tap) noexcept
{
  return tap == AudioDiagnosticLogTap::pre_opus ? "pre-opus" : "post-opus";
}

std::uint64_t elapsed_ms(std::uint64_t observed_ns, std::uint64_t started_ns) noexcept
{
  return observed_ns >= started_ns ? (observed_ns - started_ns) / 1'000'000ULL : 0;
}
} // namespace

void AudioDiagnosticSessionLog::begin(std::string_view companion_version,
                                      std::string_view obs_version,
                                      std::uint64_t started_unix_ms,
                                      std::uint64_t started_monotonic_ns)
{
  companion_version_ = companion_version;
  obs_version_ = obs_version;
  started_unix_ms_ = started_unix_ms;
  started_monotonic_ns_ = started_monotonic_ns;
  completed_monotonic_ns_ = 0;
  first_ = 0;
  size_ = 0;
  dropped_records_ = 0;
  active_ = true;
  complete_ = false;
  completed_due_to_duration_limit_ = false;
}

void AudioDiagnosticSessionLog::append(Record record) noexcept
{
  if (expire_if_due(record.observed_ns))
    return;
  if (!active_)
    return;
  if (size_ == audio_diagnostic_log_capacity)
  {
    first_ = (first_ + 1) % audio_diagnostic_log_capacity;
    --size_;
    ++dropped_records_;
  }
  records_[(first_ + size_) % audio_diagnostic_log_capacity] = record;
  ++size_;
}

void AudioDiagnosticSessionLog::playback_started(AudioDiagnosticLogTap tap,
                                                 std::uint64_t observed_ns) noexcept
{
  append({RecordKind::playback_started, tap, observed_ns, {}});
}

void AudioDiagnosticSessionLog::playback_stopped(std::uint64_t observed_ns) noexcept
{
  append({RecordKind::playback_stopped, AudioDiagnosticLogTap::pre_opus, observed_ns, {}});
}

void AudioDiagnosticSessionLog::record(AudioDiagnosticLogSnapshot snapshot) noexcept
{
  append({RecordKind::snapshot, snapshot.tap, snapshot.observed_ns, snapshot});
}

void AudioDiagnosticSessionLog::complete(std::uint64_t observed_ns) noexcept
{
  if (!active_)
    return;
  completed_monotonic_ns_ = observed_ns;
  active_ = false;
  complete_ = true;
}

bool AudioDiagnosticSessionLog::expire_if_due(std::uint64_t observed_ns) noexcept
{
  if (!active_ || observed_ns < started_monotonic_ns_ ||
      observed_ns - started_monotonic_ns_ < audio_diagnostic_log_duration_limit_ns)
  {
    return false;
  }
  completed_monotonic_ns_ = started_monotonic_ns_ + audio_diagnostic_log_duration_limit_ns;
  active_ = false;
  complete_ = true;
  completed_due_to_duration_limit_ = true;
  return true;
}

bool AudioDiagnosticSessionLog::active() const noexcept
{
  return active_;
}

bool AudioDiagnosticSessionLog::report_available() const noexcept
{
  return complete_;
}

bool AudioDiagnosticSessionLog::completed_due_to_duration_limit() const noexcept
{
  return completed_due_to_duration_limit_;
}

std::string AudioDiagnosticSessionLog::report() const
{
  if (!complete_)
    return {};
  std::ostringstream output;
  output << json{{"event", "session-start"},
                 {"schemaVersion", 1},
                 {"companionVersion", companion_version_},
                 {"obsVersion", obs_version_},
                 {"startedUnixMs", started_unix_ms_},
                 {"privacy",
                  "scalar diagnostics only; no PCM, credentials, device IDs, source names, or "
                  "network addresses"}}
                .dump()
         << '\n';
  if (dropped_records_ != 0)
    output << json{{"event", "records-dropped"}, {"count", dropped_records_}}.dump() << '\n';
  for (std::size_t offset = 0; offset < size_; ++offset)
  {
    const auto& record = records_[(first_ + offset) % audio_diagnostic_log_capacity];
    const auto at_ms = elapsed_ms(record.observed_ns, started_monotonic_ns_);
    if (record.kind == RecordKind::playback_started)
    {
      output << json{{"event", "playback-started"}, {"atMs", at_ms}, {"tap", tap_name(record.tap)}}
                    .dump()
             << '\n';
      continue;
    }
    if (record.kind == RecordKind::playback_stopped)
    {
      output << json{{"event", "playback-stopped"}, {"atMs", at_ms}}.dump() << '\n';
      continue;
    }
    const auto& value = record.snapshot;
    output << json{{"event", "audio-frame"},
                   {"atMs", at_ms},
                   {"tap", tap_name(value.tap)},
                   {"frameSequence", value.frame_sequence},
                   {"firstSampleIndex", value.first_sample_index},
                   {"capturedNs", value.captured_ns},
                   {"topologyGeneration", value.topology_generation},
                   {"frameFlags", value.frame_flags},
                   {"framePeak", value.frame_peak},
                   {"peakMaximum1s", value.maximum_peak_last_second},
                   {"peakAverage1s", value.average_peak_last_second},
                   {"peakObservations1s", value.peak_observations_last_second},
                   {"receivedFrames", value.received_frames},
                   {"playedSampleFrames", value.played_frames},
                   {"queuedFrames", value.queued_frames},
                   {"queueOverflows", value.queue_overflows},
                   {"outputUnderruns", value.output_underruns},
                   {"playbackDiscontinuities", value.discontinuities},
                   {"silentConcealmentFrames", value.silent_concealment_frames},
                   {"prebuffering", value.prebuffering},
                   {"gainDb", value.gain_db},
                   {"gainClippedSamples", value.gain_clipped_samples},
                   {"monitorAvailable", value.monitor_available},
                   {"monitorAgeMs", value.monitor_age_ms},
                   {"callbackCount", value.callback_count},
                   {"ingressOverflowDrops", value.ingress_overflow_drops},
                   {"oversizedCallbackDrops", value.oversized_callback_drops},
                   {"ipcSkippedFrames", value.ipc_skipped_frames},
                   {"timestampDiscontinuities", value.timestamp_discontinuities},
                   {"timelineGaps", value.timeline_gaps},
                   {"timelineOverlaps", value.timeline_overlaps},
                   {"monitorLatestPeak", value.monitor_latest_peak},
                   {"monitorLatestRms", value.monitor_latest_rms}}
                  .dump()
           << '\n';
  }
  output << json{{"event", "session-complete"},
                 {"atMs", elapsed_ms(completed_monotonic_ns_, started_monotonic_ns_)},
                 {"reason", completed_due_to_duration_limit_ ? "duration-limit" : "user"},
                 {"records", size_},
                 {"droppedRecords", dropped_records_}}
                .dump()
         << '\n';
  return output.str();
}

} // namespace studio_remote
