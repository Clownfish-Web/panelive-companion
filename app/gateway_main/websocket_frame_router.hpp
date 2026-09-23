#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace studio_remote
{
enum class WebSocketFrameDisposition
{
  application_text,
  keep_open,
  close_connection
};

struct WebSocketTransportDiagnostics
{
  std::uint64_t connections_opened = 0;
  std::uint64_t connections_closed = 0;
  std::uint64_t peer_close_frames = 0;
  std::uint64_t pings_received = 0;
  std::uint64_t pongs_sent = 0;
  std::uint64_t transport_errors = 0;
  std::uint64_t last_ping_unix_ms = 0;
  std::uint64_t last_pong_unix_ms = 0;
  std::uint16_t last_peer_close_code = 0;
  std::string last_peer_close_reason;
};

class WebSocketFrameRouter
{
public:
  using FrameWriter = std::function<int(int opcode, std::string_view payload)>;

  WebSocketFrameDisposition route(int bits,
                                  std::string_view payload,
                                  const FrameWriter& writer);
  void connection_opened();
  void connection_closed();
  void transport_error();
  WebSocketTransportDiagnostics diagnostics() const;

private:
  static std::uint64_t unix_time_ms();
  static std::string sanitized_close_reason(std::string_view payload);

  mutable std::mutex mutex_;
  WebSocketTransportDiagnostics diagnostics_;
};
} // namespace studio_remote
