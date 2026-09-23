#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace studio_remote
{
struct EncodedVideoFrame
{
  std::string track;
  std::uint32_t rtp_timestamp = 0;
  bool keyframe = false;
  std::span<const std::byte> payload;
};
class IWebRtcTransport
{
public:
  virtual ~IWebRtcTransport() = default;
  virtual std::string
  answer(std::string_view device_id, std::string_view offer_sdp, bool studio_mode) = 0;
  virtual void remove(std::string_view device_id) = 0;
  virtual void send_video(const EncodedVideoFrame& frame) = 0;
  virtual void send_telemetry(std::string_view json_message) = 0;
};
} // namespace studio_remote
