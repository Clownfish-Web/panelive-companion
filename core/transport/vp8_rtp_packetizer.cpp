#include "vp8_rtp_packetizer.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace studio_remote
{
namespace
{
constexpr std::size_t vp8_payload_descriptor_size = 4;
}

Vp8RtpPacketizer::Vp8RtpPacketizer(std::shared_ptr<rtc::RtpPacketizationConfig> configuration,
                                   std::size_t maximum_fragment_size)
    : RtpPacketizer(std::move(configuration)), maximum_fragment_size_(maximum_fragment_size)
{
  if (maximum_fragment_size_ <= vp8_payload_descriptor_size)
    throw std::invalid_argument("VP8 RTP fragment size is too small");
}

std::uint64_t Vp8RtpPacketizer::packet_count() const
{
  return packet_count_.load(std::memory_order_relaxed);
}

std::uint64_t Vp8RtpPacketizer::byte_count() const
{
  return byte_count_.load(std::memory_order_relaxed);
}

std::vector<rtc::binary> Vp8RtpPacketizer::fragment(rtc::binary data)
{
  const auto fragment_payload_size = maximum_fragment_size_ - vp8_payload_descriptor_size;
  std::vector<rtc::binary> fragments;
  fragments.reserve(
      std::max<std::size_t>(1, (data.size() + fragment_payload_size - 1) / fragment_payload_size));

  const auto picture_id = static_cast<std::uint16_t>(picture_id_++ & 0x7fffU);
  for (std::size_t offset = 0; offset < data.size() || fragments.empty();)
  {
    const auto data_size = std::min(fragment_payload_size, data.size() - offset);
    rtc::binary fragment(vp8_payload_descriptor_size + data_size);

    // RFC 7741 extended descriptor with a 15-bit PictureID. S is set only on the first
    // packet of the VP8 frame and PartID is zero because libvpx emits one partition here.
    fragment[0] = std::byte{static_cast<std::uint8_t>(0x80U | (offset == 0 ? 0x10U : 0U))};
    fragment[1] = std::byte{0x80};
    fragment[2] = std::byte{static_cast<std::uint8_t>(0x80U | (picture_id >> 8U))};
    fragment[3] = std::byte{static_cast<std::uint8_t>(picture_id & 0xffU)};
    if (data_size != 0)
      std::memcpy(fragment.data() + vp8_payload_descriptor_size, data.data() + offset, data_size);
    fragments.push_back(std::move(fragment));
    offset += data_size;
  }
  return fragments;
}

rtc::message_ptr Vp8RtpPacketizer::packetize(const rtc::binary& payload, bool marker)
{
  auto packet = RtpPacketizer::packetize(payload, marker);
  packet_count_.fetch_add(1, std::memory_order_relaxed);
  byte_count_.fetch_add(packet->size(), std::memory_order_relaxed);
  return packet;
}
} // namespace studio_remote
