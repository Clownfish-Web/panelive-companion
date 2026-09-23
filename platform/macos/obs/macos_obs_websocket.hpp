#pragma once

#include <memory>
#include <studio_remote/obs/obs_websocket.hpp>

namespace studio_remote::macos
{
/// Uses the existing libdatachannel WebSocket transport, as in the Linux port. Socket details and
/// callbacks remain private; no OBS protocol policy is implemented here.
class MacosObsWebSocket final : public IObsWebSocket
{
public:
  MacosObsWebSocket();
  ~MacosObsWebSocket() override;
  bool
  connect(std::string_view host, std::uint16_t port, std::chrono::milliseconds timeout) override;
  bool send(std::string message) override;
  ObsSocketRead receive(std::chrono::milliseconds timeout) override;
  void close() override;
  std::string last_error() const override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::macos
