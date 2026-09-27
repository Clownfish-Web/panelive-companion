#include "websocket_frame_router.hpp"

#include <chrono>
#include <studio_remote/common/utf8.hpp>

namespace studio_remote
{
namespace
{
constexpr int final_frame = 0x80;
constexpr int opcode_mask = 0x0f;
constexpr int text_opcode = 0x1;
constexpr int close_opcode = 0x8;
constexpr int ping_opcode = 0x9;
constexpr int pong_opcode = 0xa;

std::uint16_t close_payload_error(std::string_view payload)
{
  if (payload.empty())
    return 0;
  if (payload.size() == 1)
    return 1002;
  const auto code = static_cast<std::uint16_t>((static_cast<unsigned char>(payload[0]) << 8) |
                                               static_cast<unsigned char>(payload[1]));
  const bool standard_code = code >= 1000 && code <= 1014 && (code < 1004 || code > 1006);
  if (!standard_code && (code < 3000 || code >= 5000))
    return 1002;
  return valid_utf8(payload.substr(2)) ? 0 : 1007;
}

WebSocketFrameDisposition reject_frame(std::uint16_t code,
                                       const WebSocketFrameRouter::FrameWriter& writer)
{
  const char payload[]{static_cast<char>(code >> 8), static_cast<char>(code & 0xff)};
  // Close is best effort; the reader must stop even if the native write already failed.
  writer(close_opcode, std::string_view(payload, 2));
  return WebSocketFrameDisposition::close_connection;
}
} // namespace

WebSocketFrameDisposition
WebSocketFrameRouter::route(int bits, std::string_view payload, const FrameWriter& writer)
{
  const auto opcode = bits & opcode_mask;
  if ((bits & ~0x8f) != 0 || (bits & final_frame) == 0 ||
      (opcode != text_opcode && opcode != close_opcode && opcode != ping_opcode &&
       opcode != pong_opcode) ||
      (opcode >= close_opcode && payload.size() > 125))
  {
    transport_error();
    return reject_frame(1002, writer);
  }
  if (opcode == ping_opcode)
  {
    {
      std::lock_guard lock(mutex_);
      ++diagnostics_.pings_received;
      diagnostics_.last_ping_unix_ms = unix_time_ms();
    }
    if (writer(pong_opcode, payload) <= 0)
    {
      transport_error();
      return WebSocketFrameDisposition::close_connection;
    }
    {
      std::lock_guard lock(mutex_);
      ++diagnostics_.pongs_sent;
      diagnostics_.last_pong_unix_ms = unix_time_ms();
    }
    return WebSocketFrameDisposition::keep_open;
  }

  if (opcode == pong_opcode)
    return WebSocketFrameDisposition::keep_open;

  if (opcode == close_opcode)
    return close_peer(payload, writer);

  return WebSocketFrameDisposition::application_text;
}

WebSocketFrameDisposition WebSocketFrameRouter::close_peer(std::string_view payload,
                                                           const FrameWriter& writer)
{
  if (const auto error = close_payload_error(payload))
  {
    transport_error();
    return reject_frame(error, writer);
  }
  {
    std::lock_guard lock(mutex_);
    ++diagnostics_.peer_close_frames;
    diagnostics_.last_peer_close_code =
        payload.size() >= 2
            ? static_cast<std::uint16_t>((static_cast<unsigned char>(payload[0]) << 8) |
                                         static_cast<unsigned char>(payload[1]))
            : 0;
    diagnostics_.last_peer_close_reason = sanitized_close_reason(payload);
  }
  // Echo a valid Close before native cleanup, outside the diagnostics mutex.
  if (writer(close_opcode, payload) <= 0)
    transport_error();
  return WebSocketFrameDisposition::close_connection;
}

void WebSocketFrameRouter::connection_opened()
{
  std::lock_guard lock(mutex_);
  ++diagnostics_.connections_opened;
}

void WebSocketFrameRouter::connection_closed()
{
  std::lock_guard lock(mutex_);
  ++diagnostics_.connections_closed;
}

void WebSocketFrameRouter::transport_error()
{
  std::lock_guard lock(mutex_);
  ++diagnostics_.transport_errors;
}

WebSocketTransportDiagnostics WebSocketFrameRouter::diagnostics() const
{
  std::lock_guard lock(mutex_);
  return diagnostics_;
}

std::uint64_t WebSocketFrameRouter::unix_time_ms()
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
}

std::string WebSocketFrameRouter::sanitized_close_reason(std::string_view payload)
{
  if (payload.size() <= 2)
    return {};
  std::string reason(payload.substr(2, 123));
  for (auto& character : reason)
  {
    const auto value = static_cast<unsigned char>(character);
    if (value < 0x20 || value >= 0x7f)
      character = '?';
  }
  return reason;
}
} // namespace studio_remote
