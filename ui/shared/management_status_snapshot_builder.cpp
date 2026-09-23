#include <studio_remote/ui/management_status_snapshot_builder.hpp>

#include <cstdint>
#include <deque>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace studio_remote
{
namespace
{
constexpr std::size_t maximum_health_samples = 512;

struct TrackCounters
{
  std::uint64_t frames = 0;
  std::uint64_t drops = 0;
};

struct AudioCounters
{
  std::uint64_t opus_packets = 0;
  std::uint64_t ingress_overflow_drops = 0;
  std::uint64_t oversized_callback_drops = 0;
  std::uint64_t mixer_deadline_misses = 0;
  std::uint64_t producer_timeline_gaps = 0;
  std::uint64_t producer_timeline_overlaps = 0;
  std::uint64_t ipc_skipped_frames = 0;
  std::uint64_t ipc_sample_index_gaps = 0;
  std::uint64_t encoder_queue_drops = 0;
  std::uint64_t sender_queue_drops = 0;
  std::uint64_t sender_expired_drops = 0;
  std::uint64_t sender_sequence_skips = 0;
  std::uint64_t send_exceptions = 0;
  std::uint64_t encoder_timestamp_discontinuities = 0;
  std::uint64_t opus_failures = 0;
};

struct WebRtcCounters
{
  std::uint64_t packets = 0;
  std::uint64_t bytes = 0;
  std::uint64_t drops = 0;
};

struct HealthCounters
{
  TrackCounters program;
  TrackCounters preview;
  AudioCounters audio;
  WebRtcCounters webrtc;
};

struct HealthSample
{
  std::chrono::steady_clock::time_point observed_at;
  HealthCounters activity;
};

struct HealthWindow
{
  HealthCounters counters;
  std::chrono::steady_clock::duration duration{};
};

std::uint64_t counter_delta(std::uint64_t current, std::uint64_t baseline)
{
  return current >= baseline ? current - baseline : 0;
}

TrackCounters track_counters(const nlohmann::json& track)
{
  return {track.value("capturedFrames", 0ULL), track.value("drops", 0ULL)};
}

AudioCounters audio_counters(const nlohmann::json& audio)
{
  return {audio.value("opusPackets", 0ULL),
          audio.value("ingressOverflowDrops", 0ULL),
          audio.value("oversizedCallbackDrops", 0ULL),
          audio.value("mixerDeadlineMisses", 0ULL),
          audio.value("producerTimelineGaps", 0ULL),
          audio.value("producerTimelineOverlaps", 0ULL),
          audio.value("ipcSkippedFrames", 0ULL),
          audio.value("ipcSampleIndexGaps", 0ULL),
          audio.value("encoderQueueDrops", 0ULL),
          audio.value("senderQueueDrops", 0ULL),
          audio.value("senderExpiredDrops", 0ULL),
          audio.value("senderSequenceSkips", 0ULL),
          audio.value("sendExceptions", 0ULL),
          audio.value("encoderTimestampDiscontinuities", 0ULL),
          audio.value("opusFailures", 0ULL)};
}

WebRtcCounters webrtc_counters(const nlohmann::json& peers)
{
  WebRtcCounters counters;
  for (const auto& peer : peers)
  {
    counters.packets += peer.value("packets", 0ULL);
    counters.bytes += peer.value("bytes", 0ULL);
    counters.drops += peer.value("drops", 0ULL);
  }
  return counters;
}

ManagementFeedStatus webrtc_output_status(std::string_view provider_name,
                                          const nlohmann::json& webrtc,
                                          const WebRtcCounters& window)
{
  const auto peers = webrtc.value("peers", nlohmann::json::array());
  bool all_connected = true;
  for (const auto& peer : peers)
    all_connected = all_connected && peer.value("stateName", "unknown") == "connected";
  const auto peer_count = peers.size();
  return {std::string(provider_name) + " WebRTC media output",
          peer_count == 0 ? ManagementHealth::idle
          : all_connected ? ManagementHealth::healthy
                          : ManagementHealth::degraded,
          std::to_string(peer_count) + " peers, last 5 min: RTP packets " +
              std::to_string(window.packets) + ", bytes " + std::to_string(window.bytes) +
              ", send drops " + std::to_string(window.drops)};
}

HealthCounters health_counters(const nlohmann::json& capture, const nlohmann::json& webrtc)
{
  const auto program = capture.value("program", nlohmann::json::object());
  const auto preview = capture.value("preview", nlohmann::json::object());
  const auto audio = webrtc.value("audioMonitor", nlohmann::json::object());
  const auto peers = webrtc.value("peers", nlohmann::json::array());
  return {track_counters(program),
          track_counters(preview),
          audio_counters(audio),
          webrtc_counters(peers)};
}

TrackCounters subtract(const TrackCounters& current, const TrackCounters& baseline)
{
  return {counter_delta(current.frames, baseline.frames),
          counter_delta(current.drops, baseline.drops)};
}

AudioCounters subtract(const AudioCounters& current, const AudioCounters& baseline)
{
  return {counter_delta(current.opus_packets, baseline.opus_packets),
          counter_delta(current.ingress_overflow_drops, baseline.ingress_overflow_drops),
          counter_delta(current.oversized_callback_drops, baseline.oversized_callback_drops),
          counter_delta(current.mixer_deadline_misses, baseline.mixer_deadline_misses),
          counter_delta(current.producer_timeline_gaps, baseline.producer_timeline_gaps),
          counter_delta(current.producer_timeline_overlaps, baseline.producer_timeline_overlaps),
          counter_delta(current.ipc_skipped_frames, baseline.ipc_skipped_frames),
          counter_delta(current.ipc_sample_index_gaps, baseline.ipc_sample_index_gaps),
          counter_delta(current.encoder_queue_drops, baseline.encoder_queue_drops),
          counter_delta(current.sender_queue_drops, baseline.sender_queue_drops),
          counter_delta(current.sender_expired_drops, baseline.sender_expired_drops),
          counter_delta(current.sender_sequence_skips, baseline.sender_sequence_skips),
          counter_delta(current.send_exceptions, baseline.send_exceptions),
          counter_delta(current.encoder_timestamp_discontinuities,
                        baseline.encoder_timestamp_discontinuities),
          counter_delta(current.opus_failures, baseline.opus_failures)};
}

WebRtcCounters subtract(const WebRtcCounters& current, const WebRtcCounters& baseline)
{
  return {counter_delta(current.packets, baseline.packets),
          counter_delta(current.bytes, baseline.bytes),
          counter_delta(current.drops, baseline.drops)};
}

HealthCounters subtract(const HealthCounters& current, const HealthCounters& baseline)
{
  return {subtract(current.program, baseline.program),
          subtract(current.preview, baseline.preview),
          subtract(current.audio, baseline.audio),
          subtract(current.webrtc, baseline.webrtc)};
}

void accumulate(TrackCounters& total, const TrackCounters& increment)
{
  total.frames += increment.frames;
  total.drops += increment.drops;
}

void accumulate(AudioCounters& total, const AudioCounters& increment)
{
  total.opus_packets += increment.opus_packets;
  total.ingress_overflow_drops += increment.ingress_overflow_drops;
  total.oversized_callback_drops += increment.oversized_callback_drops;
  total.mixer_deadline_misses += increment.mixer_deadline_misses;
  total.producer_timeline_gaps += increment.producer_timeline_gaps;
  total.producer_timeline_overlaps += increment.producer_timeline_overlaps;
  total.ipc_skipped_frames += increment.ipc_skipped_frames;
  total.ipc_sample_index_gaps += increment.ipc_sample_index_gaps;
  total.encoder_queue_drops += increment.encoder_queue_drops;
  total.sender_queue_drops += increment.sender_queue_drops;
  total.sender_expired_drops += increment.sender_expired_drops;
  total.sender_sequence_skips += increment.sender_sequence_skips;
  total.send_exceptions += increment.send_exceptions;
  total.encoder_timestamp_discontinuities += increment.encoder_timestamp_discontinuities;
  total.opus_failures += increment.opus_failures;
}

void accumulate(WebRtcCounters& total, const WebRtcCounters& increment)
{
  total.packets += increment.packets;
  total.bytes += increment.bytes;
  total.drops += increment.drops;
}

void accumulate(HealthCounters& total, const HealthCounters& increment)
{
  accumulate(total.program, increment.program);
  accumulate(total.preview, increment.preview);
  accumulate(total.audio, increment.audio);
  accumulate(total.webrtc, increment.webrtc);
}

std::vector<std::string> split_listeners(std::string_view value)
{
  std::vector<std::string> result;
  while (!value.empty())
  {
    const auto comma = value.find(',');
    auto part = comma == std::string_view::npos ? value : value.substr(0, comma);
    if (!part.empty() && part.back() == 's')
      part.remove_suffix(1);
    if (!part.empty())
      result.emplace_back(part);
    if (comma == std::string_view::npos)
      break;
    value.remove_prefix(comma + 1);
  }
  return result;
}

std::string companion_detail(const CompanionEndpointStatus& companion)
{
  std::ostringstream detail;
  detail << "running";
  if (!companion.name.empty())
    detail << ", name " << companion.name;
  const auto listeners = split_listeners(companion.listening_ports);
  if (listeners.empty())
  {
    detail << ", HTTPS port " << companion.port;
    return detail.str();
  }
  detail << ", HTTPS listeners ";
  for (std::size_t index = 0; index < listeners.size(); ++index)
  {
    if (index != 0)
      detail << ", ";
    detail << listeners[index];
  }
  return detail.str();
}

std::string track_detail(const nlohmann::json& track,
                         const TrackCounters& window,
                         std::chrono::steady_clock::duration window_duration,
                         bool requested)
{
  if (!requested)
    return "idle - no client requests video";

  const auto elapsed_seconds = std::chrono::duration<double>(window_duration).count();
  const auto average_fps =
      elapsed_seconds > 0.0 ? static_cast<double>(window.frames) / elapsed_seconds : 0.0;
  std::ostringstream detail;
  detail << "last 5 min: frames " << window.frames << ", drops " << window.drops << ", average "
         << std::fixed << std::setprecision(1) << average_fps << " fps, sampled RGB "
         << track.value("sampledAverageChannel", 0.0) << ", non-black "
         << track.value("sampledNonBlackPercent", 0.0) << '%';
  if (track.contains("ageMs") && track["ageMs"].is_number_unsigned())
    detail << ", newest frame " << track["ageMs"].get<std::uint64_t>() << " ms ago";
  else
    detail << ", no frame received";
  return detail.str();
}

ManagementHealth track_health(const nlohmann::json& track, bool requested)
{
  if (!requested)
    return ManagementHealth::idle;
  return track.value("available", false) ? ManagementHealth::healthy
                                         : ManagementHealth::unavailable;
}

std::uint64_t audio_pipeline_loss_count(const AudioCounters& audio)
{
  return audio.ingress_overflow_drops + audio.oversized_callback_drops +
         audio.mixer_deadline_misses + audio.ipc_skipped_frames + audio.ipc_sample_index_gaps +
         audio.encoder_queue_drops + audio.sender_queue_drops + audio.sender_expired_drops +
         audio.sender_sequence_skips + audio.send_exceptions +
         audio.encoder_timestamp_discontinuities + audio.opus_failures;
}

std::string audio_monitor_detail(const nlohmann::json& audio, const AudioCounters& window)
{
  if (!audio.value("requested", false))
    return "idle - no client requests monitor audio";

  std::ostringstream detail;
  detail << "last 5 min: Opus packets " << window.opus_packets << ", ingress overflow "
         << window.ingress_overflow_drops << ", repaired timeline gaps/overlaps "
         << window.producer_timeline_gaps << "/" << window.producer_timeline_overlaps
         << ", IPC overwrites " << window.ipc_skipped_frames << ", encoder queue drops "
         << window.encoder_queue_drops << ", sender queue/expired drops "
         << window.sender_queue_drops << "/" << window.sender_expired_drops
         << ", RTP sequence skips " << window.sender_sequence_skips << ", state "
         << audio.value("overloadState", "NORMAL") << ", timestamp discontinuities "
         << window.encoder_timestamp_discontinuities << ", Opus failures " << window.opus_failures;
  return detail.str();
}
} // namespace

struct ManagementStatusSnapshotBuilder::Impl
{
  explicit Impl(Clock supplied_clock) : clock(std::move(supplied_clock))
  {
  }

  HealthWindow observe(const nlohmann::json& capture, const nlohmann::json& webrtc)
  {
    const auto now = clock();
    const auto current = health_counters(capture, webrtc);
    std::lock_guard lock(mutex);
    const bool clock_moved_back = last_observed_at && now < *last_observed_at;
    const bool observation_gap =
        last_observed_at && !clock_moved_back && now - *last_observed_at > management_health_window;
    if (clock_moved_back || observation_gap)
    {
      samples.clear();
      last_counters.reset();
    }

    const auto activity = last_counters ? subtract(current, *last_counters) : HealthCounters{};
    last_counters = current;
    last_observed_at = now;
    if (!samples.empty() && now - samples.back().observed_at < std::chrono::seconds(1))
      accumulate(samples.back().activity, activity);
    else
      samples.push_back({now, activity});

    const auto cutoff = now - management_health_window;
    while (samples.size() > 1 && samples.front().observed_at < cutoff)
      samples.pop_front();
    while (samples.size() > maximum_health_samples)
      samples.pop_front();

    HealthCounters window_activity;
    for (const auto& sample : samples)
      accumulate(window_activity, sample.activity);
    return {window_activity, now - samples.front().observed_at};
  }

  Clock clock;
  std::mutex mutex;
  std::deque<HealthSample> samples;
  std::optional<HealthCounters> last_counters;
  std::optional<std::chrono::steady_clock::time_point> last_observed_at;
};

ManagementStatusSnapshotBuilder::ManagementStatusSnapshotBuilder()
    : ManagementStatusSnapshotBuilder([] { return std::chrono::steady_clock::now(); })
{
}

ManagementStatusSnapshotBuilder::ManagementStatusSnapshotBuilder(Clock clock)
    : impl_(std::make_unique<Impl>(std::move(clock)))
{
}

ManagementStatusSnapshotBuilder::~ManagementStatusSnapshotBuilder() = default;

ManagementFeedStatus
ManagementStatusSnapshotBuilder::build_webrtc_output(std::string_view provider_name,
                                                     const nlohmann::json& webrtc)
{
  const auto window = impl_->observe(nlohmann::json::object(), webrtc);
  return webrtc_output_status(provider_name, webrtc, window.counters.webrtc);
}

ManagementStatusSnapshot
ManagementStatusSnapshotBuilder::build(const CompanionEndpointStatus& companion,
                                       const ObsConnectionStatus& obs,
                                       std::string_view obs_version,
                                       const nlohmann::json& capture,
                                       const nlohmann::json& audio_mixer,
                                       const nlohmann::json& thumbnails,
                                       const nlohmann::json& webrtc)
{
  const auto window = impl_->observe(capture, webrtc);
  ManagementStatusSnapshot snapshot;
  snapshot.feeds.push_back(
      {"Companion app", ManagementHealth::healthy, companion_detail(companion)});

  snapshot.feeds.push_back(webrtc_output_status(obs.backend_name, webrtc, window.counters.webrtc));

  std::string obs_detail = obs.connected ? "connected" : "disconnected";
  if (obs.synchronized)
    obs_detail += ", canonical state synchronized";
  if (!obs_version.empty())
    obs_detail += ", " + obs.backend_name + " " + std::string(obs_version);
  if (!obs.detail.empty())
    obs_detail += ", " + obs.detail;
  snapshot.feeds.push_back({obs.backend_name + " control API",
                            obs.synchronized ? ManagementHealth::healthy
                            : obs.connected  ? ManagementHealth::degraded
                                             : ManagementHealth::unavailable,
                            std::move(obs_detail)});

  if (obs.native_helper_required)
  {
    const bool ipc_connected = capture.value("ipcConnected", false);
    std::string ipc_detail = ipc_connected ? "owner-restricted frame mapping connected"
                                           : "frame mapping is not connected";
    if (const auto pid = capture.value("producerPid", 0U); pid != 0)
      ipc_detail += ", producer PID " + std::to_string(pid);
    snapshot.feeds.push_back(
        {"OBS plugin video IPC",
         ipc_connected ? ManagementHealth::healthy : ManagementHealth::unavailable,
         std::move(ipc_detail)});
  }

  const auto program = capture.value("program", nlohmann::json::object());
  const auto preview = capture.value("preview", nlohmann::json::object());
  const bool video_requested = capture.value("activeVideoClientCount", 0U) != 0;
  snapshot.feeds.push_back(
      {"OBS Program frame feed",
       track_health(program, video_requested),
       track_detail(program, window.counters.program, window.duration, video_requested)});
  snapshot.feeds.push_back(
      {"OBS Preview frame feed",
       track_health(preview, video_requested),
       track_detail(preview, window.counters.preview, window.duration, video_requested)});

  const bool roster_observed = audio_mixer.value("observed", false);
  const auto roster_count =
      audio_mixer.value("canonicalRosterUuids", nlohmann::json::array()).size();
  snapshot.feeds.push_back(
      {obs.native_helper_required ? "OBS plugin Audio Mixer IPC" : "Audio input catalog",
       roster_observed ? ManagementHealth::healthy : ManagementHealth::unavailable,
       roster_observed ? "roster received, " + std::to_string(roster_count) + " inputs"
                       : "no valid roster received"});

  const auto queued = thumbnails.value("queuedCaptures", 0ULL);
  const auto pending = thumbnails.value("pendingCaptures", 0ULL);
  const auto suppressed = thumbnails.value("negativeCacheEntries", 0ULL);
  const bool thumbnails_available = thumbnails.value("available", false);
  std::int64_t activity_age = -1;
  if (const auto age = thumbnails.find("lastActivityAgeMs");
      age != thumbnails.end() && age->is_number_integer())
  {
    activity_age = age->get<std::int64_t>();
  }
  const bool recently_active =
      activity_age >= 0 && std::chrono::milliseconds(activity_age) <= management_health_window;
  const auto thumbnail_health = !thumbnails_available ? ManagementHealth::unavailable
                                : queued > 0 || pending > 0 || recently_active
                                    ? ManagementHealth::healthy
                                    : ManagementHealth::idle;
  std::string thumbnail_detail;
  if (!thumbnails_available)
  {
    thumbnail_detail = "OBS plugin thumbnail mapping is unavailable";
  }
  else if (!recently_active && queued == 0 && pending == 0)
  {
    thumbnail_detail = "idle - no thumbnail requests in last 5 min, cache entries " +
                       std::to_string(thumbnails.value("cacheEntries", 0ULL));
  }
  else
  {
    thumbnail_detail = "last 5 min: requests handled, cache entries " +
                       std::to_string(thumbnails.value("cacheEntries", 0ULL)) +
                       ", unavailable targets " + std::to_string(suppressed) + ", queued " +
                       std::to_string(queued) + ", pending " + std::to_string(pending);
  }
  snapshot.feeds.push_back(
      {"OBS source thumbnail IPC", thumbnail_health, std::move(thumbnail_detail)});

  const auto monitor_audio = webrtc.value("audioMonitor", nlohmann::json::object());
  const bool audio_configured = monitor_audio.value("configured", false);
  const bool audio_available = monitor_audio.value("available", false);
  const bool audio_requested = monitor_audio.value("requested", false);
  const bool audio_degraded = audio_pipeline_loss_count(window.counters.audio) > 0 ||
                              monitor_audio.value("overloadState", "NORMAL") != "NORMAL";
  snapshot.feeds.push_back({"OBS monitor audio IPC",
                            !audio_configured || !audio_available ? ManagementHealth::unavailable
                            : !audio_requested                    ? ManagementHealth::idle
                            : audio_degraded                      ? ManagementHealth::degraded
                                                                  : ManagementHealth::healthy,
                            audio_monitor_detail(monitor_audio, window.counters.audio)});

  return snapshot;
}
} // namespace studio_remote
