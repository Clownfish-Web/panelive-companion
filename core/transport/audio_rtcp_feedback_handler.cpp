#include "audio_rtcp_feedback_handler.hpp"

#include <rtc/rtp.hpp>

#include <bit>
#include <chrono>
#include <utility>

namespace studio_remote
{
namespace
{
std::uint32_t network_u32(const void* value) noexcept
{
  const auto* bytes = static_cast<const std::uint8_t*>(value);
  return static_cast<std::uint32_t>(bytes[0]) << 24U | static_cast<std::uint32_t>(bytes[1]) << 16U |
         static_cast<std::uint32_t>(bytes[2]) << 8U | static_cast<std::uint32_t>(bytes[3]);
}

std::uint16_t network_u16(const void* value) noexcept
{
  const auto* bytes = static_cast<const std::uint8_t*>(value);
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[0]) << 8U | bytes[1]);
}

std::uint32_t compact_ntp_now()
{
  constexpr std::uint64_t ntp_unix_epoch_offset = 2'208'988'800ULL;
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now);
  const auto remainder = now - seconds;
  const auto fraction = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(remainder).count());
  const auto ntp_seconds = static_cast<std::uint64_t>(seconds.count()) + ntp_unix_epoch_offset;
  return static_cast<std::uint32_t>((ntp_seconds << 16U) |
                                    (fraction * 65'536ULL / 1'000'000'000ULL));
}
} // namespace

AudioRtcpFeedbackHandler::AudioRtcpFeedbackHandler(std::uint32_t media_ssrc,
                                                   std::function<void(AudioRtcpObservation)> report,
                                                   std::function<void(std::uint32_t)> nack)
    : media_ssrc_(media_ssrc), report_(std::move(report)), nack_(std::move(nack))
{
}

void AudioRtcpFeedbackHandler::incoming(rtc::message_vector& messages, const rtc::message_callback&)
{
  for (const auto& message : messages)
  {
    if (!message || message->type != rtc::Message::Control)
      continue;
    std::size_t offset = 0;
    while (offset + sizeof(rtc::RtcpHeader) <= message->size())
    {
      const auto* header = reinterpret_cast<const rtc::RtcpHeader*>(message->data() + offset);
      const auto packet_size = header->lengthInBytes();
      if (packet_size < sizeof(rtc::RtcpHeader) || offset + packet_size > message->size())
        break;
      if (header->payloadType() == 201 &&
          packet_size >= rtc::RtcpRr::SizeWithReportBlocks(header->reportCount()))
      {
        const auto* receiver_report = reinterpret_cast<const rtc::RtcpRr*>(header);
        for (std::uint8_t index = 0; index < header->reportCount(); ++index)
        {
          const auto* block = receiver_report->getReportBlock(index);
          if (!block || block->getSSRC() != media_ssrc_)
            continue;
          std::uint32_t round_trip_ms = 0;
          const auto loss = network_u32(&block->_fractionLostAndPacketsLost);
          auto packets_lost = static_cast<std::int32_t>(loss & 0x00FFFFFFU);
          if ((loss & 0x00800000U) != 0)
            packets_lost -= 0x01000000;
          const auto last_sender_report = network_u32(&block->_lastReport);
          if (last_sender_report != 0)
          {
            const auto now = compact_ntp_now();
            const auto delay = block->delaySinceSR();
            const auto round_trip_units = now - last_sender_report - delay;
            if (round_trip_units < 60U * 65'536U)
              round_trip_ms = static_cast<std::uint32_t>(
                  (static_cast<std::uint64_t>(round_trip_units) * 1'000ULL) / 65'536ULL);
          }
          report_({static_cast<std::uint8_t>(loss >> 24U),
                   packets_lost,
                   block->jitter(),
                   round_trip_ms,
                   block->extendedHighestSeqNo()});
        }
      }
      else if (header->payloadType() == 205 && header->reportCount() == 1 &&
               packet_size >= sizeof(rtc::RtcpFbHeader) + sizeof(std::uint32_t))
      {
        const auto* feedback = message->data() + offset + sizeof(rtc::RtcpFbHeader);
        const auto parts = (packet_size - sizeof(rtc::RtcpFbHeader)) / sizeof(std::uint32_t);
        std::uint32_t requested_packets = 0;
        for (std::size_t part = 0; part < parts; ++part)
        {
          ++requested_packets;
          requested_packets += std::popcount(network_u16(feedback + part * 4 + 2));
        }
        nack_(requested_packets);
      }
      offset += packet_size;
    }
  }
}

} // namespace studio_remote
