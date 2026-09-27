#pragma once
#include "rtp_ssrc_allocator.hpp"
#include <condition_variable>
#include <functional>
#include <rtc/rtc.hpp>
#include <studio_remote/capture/capture_provider.hpp>
#include <studio_remote/media/audio_level_telemetry.hpp>
#include <studio_remote/media/audio_monitor.hpp>
#include <studio_remote/media/audio_pipeline_hardening.hpp>
#include <studio_remote/media/bounded_audio_frame_queue.hpp>
#include <studio_remote/media/media.hpp>
#include <studio_remote/media/realtime_audio_services.hpp>
#include <studio_remote/media/video_demand.hpp>
#include <studio_remote/state/state_store.hpp>
#include <unordered_set>
#include <variant>

namespace studio_remote
{
class Vp8VideoEncoder;
struct VideoTrackPreferences
{
  std::uint32_t target_fps = 15;
  std::uint32_t max_bitrate_kbps = 1800;

  bool operator==(const VideoTrackPreferences&) const = default;
};

struct VideoOfferPreferences
{
  VideoTrackPreferences program;
  VideoTrackPreferences preview;
  bool audio_monitor = false;
  bool audio_master = false;
};

struct EncodedVideoDimensions
{
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  bool operator==(const EncodedVideoDimensions&) const = default;
};

enum class MediaSessionRetirementStatus
{
  retired,
  no_active_session,
  session_mismatch,
  failed
};

struct MediaSessionRetirementResult
{
  MediaSessionRetirementStatus status = MediaSessionRetirementStatus::failed;
  std::string media_session_id;
  std::uint64_t cleanup_failures = 0;
};

enum class MediaOfferFailure
{
  device_removed,
  capture_reset,
  hub_stopped,
  transport_closed,
  capacity_exceeded,
  sdp_limits_exceeded,
  invalid_sdp
};

using MediaOfferResult = std::variant<json, MediaOfferFailure>;

VideoTrackPreferences normalize_video_preferences(VideoTrackPreferences preferences);
EncodedVideoDimensions encoded_video_dimensions(std::uint32_t width, std::uint32_t height);

class WebRtcHub
{
public:
  using DiagnosticEventHandler = std::function<void(std::string_view, json)>;

  explicit WebRtcHub(StateStore& state, RealtimeAudioServices* realtime_audio = nullptr);
  ~WebRtcHub();
  // The application lifecycle thread serializes start/stop. Stop drains answer owners and must
  // not be called from an answer or RTC callback.
  void start();
  void stop();
  // Revalidate authority after pending registration, outside transport locks. The caller owns
  // the callback and its dependencies until this synchronous operation returns.
  MediaOfferResult answer(std::string_view device_id,
                          std::string_view offer_sdp,
                          VideoOfferPreferences preferences = {},
                          const std::function<bool()>& authorized = {});
  void remove_device(std::string_view device_id);
  MediaSessionRetirementResult retire_device_session(std::string_view device_id,
                                                     std::string_view media_session_id);
  void push_frame(std::string_view track, std::shared_ptr<const RawVideoFrame> frame);
  void restart_video_source() noexcept;
  void restart_video_sessions() noexcept;
  void push_audio_monitor(SharedAudioMonitorFrame frame);
  void push_audio_master(SharedAudioMonitorFrame frame);
  void set_post_opus_diagnostic_handler(std::function<void(SharedAudioMonitorFrame)> handler);
  void set_post_opus_diagnostic_enabled(bool enabled) noexcept;
  void set_audio_monitor_status(AudioMonitorStatus status);
  AudioMonitorStatus audio_monitor_status() const;
  void set_audio_master_status(AudioMonitorStatus status);
  AudioMonitorStatus audio_master_status() const;
  json health() const;
  std::uint32_t capture_target_fps(std::string_view track) const;
  VideoDemand video_demand() const;
  void on_diagnostic_event(DiagnosticEventHandler handler);

private:
  struct Peer;
  using Encoder = Vp8VideoEncoder;
  class PendingOffer;
  void remove_peer(std::string_view device_id, const std::shared_ptr<Peer>& expected);
  void retire_peer(const std::shared_ptr<Peer>& peer, std::string_view reason);
  void request_keyframe(std::string_view track);
  void observe_source_frame(std::string_view track);
  void observe_encoder_input(std::string_view track, const VideoTrackPreferences& policy);
  void encode_video_frame(std::string_view track,
                          const VideoTrackPreferences& policy,
                          const RawVideoFrame& frame,
                          EncodedVideoDimensions dimensions,
                          std::uint32_t rtp_timestamp,
                          std::uint64_t requested_keyframe,
                          std::uint64_t& completed_keyframe,
                          std::unique_ptr<Encoder>& encoder);
  void record_encoder_failure(std::string_view track,
                              const VideoTrackPreferences& policy,
                              std::string_view stage,
                              std::string_view reason);
  bool has_open_track(std::string_view track) const;
  std::vector<VideoTrackPreferences> active_policies(std::string_view track) const;
  void encode_loop(std::string track);
  void audio_encode_loop();
  void audio_sender_loop();
  void audio_peer_sender_loop(Peer& peer);
  void audio_complexity_benchmark();
  void audio_master_encode_loop();
  void audio_master_peer_sender_loop(Peer& peer);
  void broadcast_frame(std::string_view track,
                       const VideoTrackPreferences& policy,
                       std::span<const uint8_t> encoded,
                       uint32_t timestamp,
                       bool keyframe);
  void telemetry_loop();
  void broadcast_audio(const EncodedOpusFrame& encoded);
  void emit_diagnostic_event(std::string_view event, json details = json::object()) const;
  std::uint64_t performance_counter() const noexcept;
  std::uint64_t performance_counter_us(std::uint64_t delta) const noexcept;
  void assign_track_ssrcs(Peer& peer);
  StateStore& state_;
  RtpSsrcAllocator ssrc_allocator_;
  std::mutex ssrc_allocator_mutex_;
  RealtimeAudioServices* realtime_audio_ = nullptr;
  std::atomic<bool> stopping_{true};
  bool rtc_lifetime_acquired_ = false;
  mutable std::mutex mutex_;
  mutable std::mutex diagnostic_event_mutex_;
  DiagnosticEventHandler diagnostic_event_handler_;
  std::condition_variable frame_cv_;
  std::unordered_map<std::string, std::shared_ptr<Peer>> peers_;
  // Entries retain pending resources until their answer owner finishes cleanup. Both collections
  // and publication/cancellation ordering belong to mutex_.
  std::unordered_set<std::shared_ptr<Peer>> pending_offers_;
  std::condition_variable pending_offers_finished_;
  std::uint64_t video_demand_epoch_ = 0;
  std::unordered_map<std::string, std::shared_ptr<const RawVideoFrame>> latest_frames_;
  std::unordered_map<std::string, uint64_t> frame_versions_, encoded_versions_;
  std::uint64_t program_source_generation_ = 1;
  std::uint64_t preview_source_generation_ = 1;
  std::atomic<std::size_t> retained_program_policies_{0};
  std::atomic<std::size_t> retained_preview_policies_{0};
  BoundedAudioFrameQueue audio_frames_{3};
  BoundedEncodedAudioQueue encoded_audio_frames_;
  BoundedAudioFrameQueue audio_master_frames_{3};
  AudioOverloadController audio_overload_controller_;
  FixedLatencyHistogram audio_encode_latency_;
  FixedLatencyHistogram audio_send_latency_;
  FixedLatencyHistogram audio_departure_interval_;
  AudioFrameTelemetryRing audio_frame_telemetry_;
  AudioLevelTelemetry audio_level_telemetry_;
  AudioLevelTelemetry audio_master_level_telemetry_;
  AudioMonitorStatus audio_monitor_status_;
  AudioMonitorStatus audio_master_status_;
  std::atomic<bool> audio_master_encoder_failed_{false};
  std::atomic<std::uint64_t> audio_master_queue_drops_{0};
  std::atomic<std::uint64_t> audio_master_encoded_packets_{0};
  std::atomic<bool> audio_encoder_failed_{false};
  std::atomic<std::uint64_t> audio_queue_drops_{0};
  std::atomic<std::uint64_t> audio_input_sequence_gaps_{0};
  std::atomic<std::uint64_t> audio_timestamp_discontinuities_{0};
  std::atomic<std::uint64_t> audio_opus_failures_{0};
  std::atomic<std::uint64_t> audio_encoded_packets_{0};
  std::atomic<std::uint64_t> audio_sender_queue_drops_{0};
  std::atomic<std::uint64_t> audio_sender_expired_drops_{0};
  std::atomic<std::uint64_t> audio_send_exceptions_{0};
  std::atomic<bool> audio_peer_push_failure_{false};
  std::atomic<std::uint64_t> audio_encode_cycles_{0};
  std::atomic<std::uint64_t> audio_send_cycles_{0};
  std::atomic<std::uint64_t> audio_encode_p99_us_{0};
  std::atomic<std::uint64_t> audio_send_p99_us_{0};
  std::atomic<std::uint64_t> audio_encode_p95_us_{0};
  std::atomic<std::uint64_t> next_audio_epoch_{1};
  std::atomic<std::uint64_t> audio_overload_transitions_{0};
  std::atomic<bool> audio_recovery_requested_{false};
  std::atomic<std::uint8_t> audio_overload_state_{0};
  std::atomic<std::uint8_t> audio_profile_step_{0};
  std::atomic<std::uint64_t> audio_encoder_mmcss_failures_{0};
  std::atomic<std::uint64_t> audio_sender_mmcss_failures_{0};
  std::array<std::atomic<std::uint64_t>, 4> audio_complexity_p99_us_{};
  std::array<std::atomic<std::uint64_t>, 4> audio_complexity_p99_cycles_{};
  std::atomic<int> audio_normal_complexity_{6};
  std::atomic<bool> audio_benchmark_complete_{false};
  std::atomic<std::uint64_t> audio_pacing_resets_{0};
  std::atomic<std::uint64_t> audio_pacing_late_frames_{0};
  std::atomic<std::uint64_t> audio_departure_bursts_{0};
  std::atomic<std::uint64_t> audio_departure_gaps_{0};
  std::atomic<std::uint64_t> audio_maximum_pacing_lateness_us_{0};
  std::atomic<std::uint32_t> audio_opus_lookahead_{0};
  std::atomic<bool> post_opus_diagnostic_enabled_{false};
  std::atomic<std::uint64_t> post_opus_diagnostic_decode_failures_{0};
  std::function<void(SharedAudioMonitorFrame)> post_opus_diagnostic_handler_;
  std::unordered_map<std::string, std::unique_ptr<Encoder>> encoders_;
  std::thread program_thread_, preview_thread_, audio_thread_, audio_sender_thread_,
      audio_master_thread_, audio_benchmark_thread_, telemetry_thread_;
  std::atomic<std::uint64_t> next_media_session_id_{1};
  std::atomic<std::uint64_t> program_keyframe_requests_{1}, preview_keyframe_requests_{1};
  std::atomic<std::uint64_t> program_encoder_failures_{0}, preview_encoder_failures_{0};
};
} // namespace studio_remote
