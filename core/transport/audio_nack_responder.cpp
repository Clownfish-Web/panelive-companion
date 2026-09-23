#include "audio_nack_responder.hpp"

#include <rtc/rtp.hpp>

#include <utility>

namespace studio_remote
{

AudioNackResponder::AudioNackResponder(std::function<void(std::uint32_t retransmitted,
                                                          std::uint32_t deadline_missed,
                                                          std::uint32_t send_failed)> report)
    : report_(std::move(report))
{
}

rtc::message_ptr AudioNackResponder::find(std::uint16_t sequence_number) const
{
  std::lock_guard lock(mutex_);
  for (const auto& entry : entries_)
    if (entry.packet && entry.sequence_number == sequence_number)
      return entry.packet;
  return {};
}

void AudioNackResponder::incoming(rtc::message_vector& messages, const rtc::message_callback& send)
{
  std::uint32_t retransmitted = 0;
  std::uint32_t deadline_missed = 0;
  std::uint32_t send_failed = 0;
  const auto retransmit = [&](std::uint16_t sequence_number)
  {
    if (auto packet = find(sequence_number))
    {
      try
      {
        send(packet);
        ++retransmitted;
      }
      catch (...)
      {
        ++send_failed;
      }
    }
    else
    {
      ++deadline_missed;
    }
  };
  for (const auto& message : messages)
  {
    if (!message || message->type != rtc::Message::Control)
      continue;
    std::size_t offset = 0;
    while (offset + sizeof(rtc::RtcpFbHeader) + sizeof(rtc::RtcpNackPart) <= message->size())
    {
      auto* nack = reinterpret_cast<rtc::RtcpNack*>(message->data() + offset);
      const auto packet_size = nack->header.header.lengthInBytes();
      if (packet_size < sizeof(rtc::RtcpFbHeader) || offset + packet_size > message->size())
        break;
      if (nack->header.header.payloadType() == 205 && nack->header.header.reportCount() == 1)
      {
        const auto parts = (packet_size - sizeof(rtc::RtcpFbHeader)) / sizeof(rtc::RtcpNackPart);
        for (std::size_t part_index = 0; part_index < parts; ++part_index)
        {
          auto part = nack->parts[part_index];
          const auto packet_id = part.pid();
          retransmit(packet_id);
          const auto lost_packet_mask = part.blp();
          for (std::uint16_t bit = 0; bit < 16; ++bit)
            if ((lost_packet_mask & (1U << bit)) != 0)
              retransmit(static_cast<std::uint16_t>(packet_id + bit + 1));
        }
      }
      offset += packet_size;
    }
  }
  if (report_ && (retransmitted != 0 || deadline_missed != 0 || send_failed != 0))
    report_(retransmitted, deadline_missed, send_failed);
}

void AudioNackResponder::outgoing(rtc::message_vector& messages, const rtc::message_callback&)
{
  std::lock_guard lock(mutex_);
  for (const auto& message : messages)
  {
    if (!message || message->type == rtc::Message::Control ||
        message->size() < sizeof(rtc::RtpHeader))
      continue;
    const auto* rtp = reinterpret_cast<const rtc::RtpHeader*>(message->data());
    entries_[next_entry_] = {rtp->seqNumber(), message};
    next_entry_ = (next_entry_ + 1) % entries_.size();
  }
}

} // namespace studio_remote
