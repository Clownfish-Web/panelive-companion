#pragma once

#include <rtc/rtppacketizer.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace studio_remote
{
class Vp8RtpPacketizer final : public rtc::RtpPacketizer
{
public:
  explicit Vp8RtpPacketizer(std::shared_ptr<rtc::RtpPacketizationConfig> configuration,
                            std::size_t maximum_fragment_size = DefaultMaxFragmentSize);

  std::uint64_t packet_count() const;
  std::uint64_t byte_count() const;

protected:
  std::vector<rtc::binary> fragment(rtc::binary data) override;
  rtc::message_ptr packetize(const rtc::binary& payload, bool marker) override;

private:
  std::size_t maximum_fragment_size_;
  std::uint16_t picture_id_ = 0;
  std::atomic<std::uint64_t> packet_count_{0};
  std::atomic<std::uint64_t> byte_count_{0};
};
} // namespace studio_remote
