#pragma once

#include <rtc/mediahandler.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace studio_remote
{

struct AudioEgressWindow
{
  std::uint64_t window = 0;
  std::uint32_t packets = 0;
  std::uint16_t first_sequence = 0;
  std::uint16_t last_sequence = 0;
  std::uint32_t first_rtp_timestamp = 0;
  std::uint32_t last_rtp_timestamp = 0;
  std::uint32_t inter_packet_p50_us = 0;
  std::uint32_t inter_packet_p95_us = 0;
  std::uint32_t inter_packet_max_us = 0;
  std::uint32_t inter_packet_over_40_ms = 0;
  std::uint32_t continuity_violations = 0;
  std::uint32_t sender_reports = 0;
  std::uint32_t last_sender_report_rtp_timestamp = 0;
  std::uint8_t dscp = 46;
};

// Runs after RTP packetization, enforces the audio DSCP marking, and retains metadata only.
class AudioEgressObserver final : public rtc::MediaHandler
{
public:
  using Clock = std::chrono::steady_clock;

  void outgoing(rtc::message_vector& messages, const rtc::message_callback& send) override;
  void observe_packet(std::uint16_t sequence,
                      std::uint32_t rtp_timestamp,
                      Clock::time_point now) noexcept;
  void observe_sender_report(std::uint32_t rtp_timestamp) noexcept;
  AudioEgressWindow snapshot() const noexcept;

private:
  struct MutableWindow
  {
    AudioEgressWindow values;
    std::array<std::uint32_t, 128> intervals_us{};
    std::size_t interval_count = 0;
    Clock::time_point started_at{};
  };

  static AudioEgressWindow finalize(const MutableWindow& window) noexcept;
  void begin_window(std::uint64_t number, Clock::time_point now) noexcept;

  mutable std::mutex mutex_;
  MutableWindow current_;
  MutableWindow completed_;
  bool initialized_ = false;
  bool have_previous_packet_ = false;
  std::uint16_t previous_sequence_ = 0;
  std::uint32_t previous_rtp_timestamp_ = 0;
  Clock::time_point previous_departure_{};
  bool have_sender_report_ = false;
  Clock::time_point last_sender_report_at_{};
};

} // namespace studio_remote
