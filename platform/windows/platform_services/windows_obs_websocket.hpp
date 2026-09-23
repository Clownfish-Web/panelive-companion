#pragma once

#include <studio_remote/obs/obs_websocket.hpp>

#include <memory>
#include <mutex>

namespace studio_remote::windows
{
class WinHttpObsSocketConnection;

class WindowsObsWebSocket final : public IObsWebSocket
{
public:
  WindowsObsWebSocket() = default;
  ~WindowsObsWebSocket() override;
  bool
  connect(std::string_view host, std::uint16_t port, std::chrono::milliseconds timeout) override;
  bool send(std::string message) override;
  ObsSocketRead receive(std::chrono::milliseconds timeout) override;
  void close() override;
  std::string last_error() const override;

private:
  std::shared_ptr<WinHttpObsSocketConnection> connection() const;
  mutable std::mutex mutex_;
  std::uint64_t revision_ = 0;
  std::shared_ptr<WinHttpObsSocketConnection> connection_;
};
} // namespace studio_remote::windows
