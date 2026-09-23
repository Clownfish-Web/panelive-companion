#pragma once

#include "audio_egress_observer.hpp"
#include "libdatachannel_transport.hpp"
#include "vp8_rtp_packetizer.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace studio_remote
{
struct WebRtcHub::Peer
{
  void cancel_offer(MediaOfferFailure reason)
  {
    std::lock_guard lock(description_mutex);
    if (!offer_failure)
      offer_failure = reason;
    description_cv.notify_all();
  }

  std::optional<MediaOfferFailure> pending_failure()
  {
    std::lock_guard lock(description_mutex);
    return offer_failure;
  }

  std::uint64_t retire() noexcept
  {
    if (retiring.exchange(true, std::memory_order_acq_rel))
      return retirement_failures.load(std::memory_order_relaxed);

    audio_stopping.store(true, std::memory_order_release);
    audio_master_stopping.store(true, std::memory_order_release);
    audio_cv.notify_all();
    audio_master_cv.notify_all();
    const auto close = [this](const auto& transport)
    {
      try
      {
        if (transport)
          transport->close();
      }
      catch (...)
      {
        retirement_failures.fetch_add(1, std::memory_order_relaxed);
      }
    };
    close(program);
    close(preview);
    close(audio_monitor);
    close(audio_master);
    close(telemetry);
    close(connection);
    if (audio_sender_thread.joinable() &&
        audio_sender_thread.get_id() != std::this_thread::get_id())
      audio_sender_thread.join();
    if (audio_master_sender_thread.joinable() &&
        audio_master_sender_thread.get_id() != std::this_thread::get_id())
      audio_master_sender_thread.join();
    return retirement_failures.load(std::memory_order_relaxed);
  }

  ~Peer()
  {
    static_cast<void>(retire());
  }

  std::string device_id;
  std::string media_session_id;
  std::shared_ptr<rtc::PeerConnection> connection;
  std::shared_ptr<rtc::Track> program, preview, audio_monitor, audio_master;
  std::shared_ptr<rtc::DataChannel> telemetry;
  // Assigned by WebRtcHub::assign_track_ssrcs before any track is created.
  std::uint32_t program_ssrc = 0;
  std::uint32_t preview_ssrc = 0;
  std::uint32_t audio_monitor_ssrc = 0;
  std::uint32_t audio_master_ssrc = 0;
  std::uint64_t audio_epoch = 0;
  std::uint8_t program_payload_type = 96;
  std::uint8_t preview_payload_type = 96;
  std::shared_ptr<Vp8RtpPacketizer> program_packetizer, preview_packetizer;
  std::shared_ptr<rtc::OpusRtpPacketizer> audio_monitor_packetizer;
  std::shared_ptr<rtc::OpusRtpPacketizer> audio_master_packetizer;
  std::shared_ptr<AudioEgressObserver> audio_egress_observer;
  std::mutex description_mutex;
  std::condition_variable description_cv;
  std::optional<std::string> local_sdp;
  std::optional<MediaOfferFailure> offer_failure;
  std::atomic<std::uint64_t> program_source_frames{0}, preview_source_frames{0};
  std::atomic<std::uint64_t> program_encoder_inputs{0}, preview_encoder_inputs{0};
  std::atomic<std::uint64_t> program_frames{0}, preview_frames{0};
  std::atomic<std::uint64_t> program_send_attempts{0}, preview_send_attempts{0};
  std::atomic<std::uint64_t> program_send_failures{0}, preview_send_failures{0};
  std::atomic<std::uint64_t> program_rtcp_feedback{0}, preview_rtcp_feedback{0};
  std::atomic<std::uint64_t> drops{0};
  std::atomic<std::uint64_t> audio_monitor_packets{0}, audio_monitor_bytes{0},
      audio_monitor_drops{0};
  BoundedEncodedAudioQueue encoded_audio_frames;
  AudioOverloadController audio_overload_controller;
  std::mutex audio_wait_mutex;
  std::condition_variable audio_cv;
  std::atomic<bool> audio_stopping{false};
  std::atomic<bool> removal_requested{false};
  std::atomic<bool> retiring{false};
  std::atomic<std::uint64_t> retirement_failures{0};
  std::thread audio_sender_thread;
  // Written before publication; callbacks never inspect thread objects racing a join.
  std::thread::id audio_sender_id;
  BoundedEncodedAudioQueue encoded_audio_master_frames;
  std::mutex audio_master_wait_mutex;
  std::condition_variable audio_master_cv;
  std::atomic<bool> audio_master_stopping{false};
  std::thread audio_master_sender_thread;
  std::thread::id audio_master_sender_id;
  std::atomic<std::uint64_t> audio_master_packets{0}, audio_master_bytes{0}, audio_master_drops{0},
      audio_master_sequence_skips{0};
  std::atomic<std::uint32_t> audio_master_last_rtp_timestamp{0};
  std::atomic<bool> audio_master_rtp_timeline_started{false};
  std::atomic<std::uint64_t> audio_sender_queue_drops{0}, audio_sender_expired_drops{0},
      audio_sender_sequence_skips{0}, audio_send_p95_us{0}, audio_send_p99_us{0},
      audio_send_cycles{0}, audio_mmcss_failures{0}, audio_send_exceptions{0};
  // The transport reports a refused packet by throwing; the last text is kept so diagnostics can
  // name the cause instead of only counting it.
  std::mutex audio_send_error_mutex;
  std::string audio_last_send_error;

  void record_send_error(std::string_view message)
  {
    std::lock_guard lock(audio_send_error_mutex);
    audio_last_send_error.assign(message);
  }

  std::string last_send_error()
  {
    std::lock_guard lock(audio_send_error_mutex);
    return audio_last_send_error;
  }
  std::atomic<std::uint64_t> audio_last_send_start_ticks{0}, audio_last_departure_interval_us{0};
  std::atomic<std::uint32_t> audio_last_rtp_timestamp{0}, audio_last_topology_generation{0};
  std::atomic<bool> audio_rtp_timeline_started{false};
  std::atomic<std::uint8_t> audio_overload_state{0}, audio_profile_step{0};
  std::atomic<std::uint64_t> audio_rtcp_reports{0}, audio_nacks_received{0},
      audio_nacks_retransmitted{0}, audio_nack_deadline_misses{0}, audio_nack_send_failures{0};
  std::atomic<std::uint32_t> audio_fraction_lost{0}, audio_jitter{0}, audio_round_trip_ms{0};
  std::atomic<std::uint32_t> audio_extended_highest_sequence{0};
  std::atomic<std::int32_t> audio_packets_lost{0};
  std::atomic<std::uint64_t> android_audio_reports{0}, android_plc_frames{0},
      android_audio_track_underruns{0}, android_output_frames{0};
  std::atomic<std::uint32_t> android_jitter_buffer_delay_ms{0};
  std::atomic<std::uint64_t> program_last_timestamp{0}, preview_last_timestamp{0};
  std::atomic<bool> mixer_open{false};
  VideoTrackPreferences program_preferences;
  VideoTrackPreferences preview_preferences;
};

} // namespace studio_remote
