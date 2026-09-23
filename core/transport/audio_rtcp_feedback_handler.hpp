#pragma once

#include <rtc/mediahandler.hpp>

#include <cstdint>
#include <functional>

namespace studio_remote
{

struct AudioRtcpObservation
{
  std::uint8_t fraction_lost = 0;
  std::int32_t packets_lost = 0;
  std::uint32_t jitter = 0;
  std::uint32_t round_trip_ms = 0;
  std::uint32_t extended_highest_sequence = 0;
};

class AudioRtcpFeedbackHandler final : public rtc::MediaHandler
{
public:
  AudioRtcpFeedbackHandler(std::uint32_t media_ssrc,
                           std::function<void(AudioRtcpObservation)> report,
                           std::function<void(std::uint32_t)> nack);

  void incoming(rtc::message_vector& messages, const rtc::message_callback& send) override;

private:
  std::uint32_t media_ssrc_ = 0;
  std::function<void(AudioRtcpObservation)> report_;
  std::function<void(std::uint32_t)> nack_;
};

} // namespace studio_remote
