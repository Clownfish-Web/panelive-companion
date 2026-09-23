#include "audio_egress_observer.hpp"

#include <rtc/rtp.hpp>

#include <algorithm>

namespace studio_remote
{
namespace
{
constexpr std::uint32_t audio_packet_samples = 960;
constexpr std::uint8_t expedited_forwarding_dscp = 46;
} // namespace

void AudioEgressObserver::outgoing(rtc::message_vector& messages, const rtc::message_callback&)
{
  const auto now = Clock::now();
  for (const auto& message : messages)
  {
    if (!message)
      continue;
    message->dscp = expedited_forwarding_dscp;
    if (message->type == rtc::Message::Control || message->size() < sizeof(rtc::RtpHeader))
      continue;
    const auto* header = reinterpret_cast<const rtc::RtpHeader*>(message->data());
    if (header->version() != 2)
      continue;
    observe_packet(header->seqNumber(), header->timestamp(), now);
  }
}

void AudioEgressObserver::begin_window(std::uint64_t number, Clock::time_point now) noexcept
{
  current_ = {};
  current_.values.window = number;
  current_.started_at = now;
}

void AudioEgressObserver::observe_packet(std::uint16_t sequence,
                                         std::uint32_t rtp_timestamp,
                                         Clock::time_point now) noexcept
{
  std::lock_guard lock(mutex_);
  if (!initialized_)
  {
    begin_window(1, now);
    initialized_ = true;
  }

  std::uint32_t interval_us = 0;
  if (have_previous_packet_)
  {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(now - previous_departure_);
    interval_us =
        static_cast<std::uint32_t>(std::clamp<std::int64_t>(elapsed.count(), 0, UINT32_MAX));
  }

  if (now - current_.started_at >= std::chrono::seconds(1))
  {
    completed_ = current_;
    begin_window(completed_.values.window + 1, now);
  }

  auto& values = current_.values;
  if (values.packets == 0)
  {
    values.first_sequence = sequence;
    values.first_rtp_timestamp = rtp_timestamp;
  }
  values.last_sequence = sequence;
  values.last_rtp_timestamp = rtp_timestamp;
  ++values.packets;

  if (have_previous_packet_)
  {
    if (current_.interval_count < current_.intervals_us.size())
      current_.intervals_us[current_.interval_count++] = interval_us;
    if (interval_us > 40'000)
      ++values.inter_packet_over_40_ms;
    const auto sequence_delta = static_cast<std::uint16_t>(sequence - previous_sequence_);
    const auto timestamp_delta = rtp_timestamp - previous_rtp_timestamp_;
    if (sequence_delta == 0 || timestamp_delta != audio_packet_samples * sequence_delta)
      ++values.continuity_violations;
  }

  have_previous_packet_ = true;
  previous_sequence_ = sequence;
  previous_rtp_timestamp_ = rtp_timestamp;
  previous_departure_ = now;
  if (!have_sender_report_ || now - last_sender_report_at_ >= std::chrono::seconds(1))
  {
    ++values.sender_reports;
    values.last_sender_report_rtp_timestamp = rtp_timestamp;
    have_sender_report_ = true;
    last_sender_report_at_ = now;
  }
}

void AudioEgressObserver::observe_sender_report(std::uint32_t rtp_timestamp) noexcept
{
  std::lock_guard lock(mutex_);
  if (!initialized_)
  {
    begin_window(1, Clock::now());
    initialized_ = true;
  }
  ++current_.values.sender_reports;
  current_.values.last_sender_report_rtp_timestamp = rtp_timestamp;
}

AudioEgressWindow AudioEgressObserver::finalize(const MutableWindow& window) noexcept
{
  auto result = window.values;
  if (window.interval_count == 0)
    return result;
  auto intervals = window.intervals_us;
  std::sort(intervals.begin(),
            intervals.begin() + static_cast<std::ptrdiff_t>(window.interval_count));
  const auto percentile = [&](std::size_t percent)
  { return intervals[(window.interval_count - 1) * percent / 100]; };
  result.inter_packet_p50_us = percentile(50);
  result.inter_packet_p95_us = percentile(95);
  result.inter_packet_max_us = intervals[window.interval_count - 1];
  return result;
}

AudioEgressWindow AudioEgressObserver::snapshot() const noexcept
{
  MutableWindow window;
  {
    std::lock_guard lock(mutex_);
    window = completed_.values.window != 0 ? completed_ : current_;
  }
  return finalize(window);
}

} // namespace studio_remote
