#include "websocket_frame_router.hpp"

#include <chrono>

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
} // namespace

WebSocketFrameDisposition WebSocketFrameRouter::route(int bits,
                                                       std::string_view payload,
                                                       const FrameWriter& writer)
{
  const auto opcode = bits & opcode_mask;
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
  {
    std::lock_guard lock(mutex_);
    ++diagnostics_.peer_close_frames;
    diagnostics_.last_peer_close_code =
        payload.size() >= 2
            ? static_cast<std::uint16_t>((static_cast<unsigned char>(payload[0]) << 8) |
                                         static_cast<unsigned char>(payload[1]))
            : 0;
    diagnostics_.last_peer_close_reason = sanitized_close_reason(payload);
    return WebSocketFrameDisposition::close_connection;
  }

  // The mobile protocol accepts complete text messages only. Continuations,
  // fragmented text, binary data, and reserved opcodes never reach JSON parsing.
  if (opcode == text_opcode && (bits & final_frame) != 0)
    return WebSocketFrameDisposition::application_text;
  return WebSocketFrameDisposition::keep_open;
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
