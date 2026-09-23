#pragma once

#include <studio_remote/obs/obs_websocket.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <rtc/websocket.hpp>

namespace studio_remote::macos
{
/// The adapter and active I/O calls share a connection. Library callbacks borrow it weakly.
/// Mutable receive state belongs to mutex_; socket API calls belong to socket_operation_mutex_.
class ObsSocketConnection final
{
public:
  explicit ObsSocketConnection(std::chrono::milliseconds timeout);
  ~ObsSocketConnection();
  void install_callbacks(const std::shared_ptr<ObsSocketConnection>& owner);
  bool open(const std::string& url, std::chrono::milliseconds timeout);
  bool send(std::string message);
  ObsSocketRead receive(std::chrono::milliseconds timeout);
  void close();
  std::string last_error() const;

private:
  enum class Phase
  {
    connecting,
    open,
    closed
  };
  enum class Failure
  {
    none,
    transport,
    timeout,
    binary_message,
    receive_overflow,
    send_overflow
  };
  static std::string error_text(Failure failure);
  void received(rtc::message_variant value);
  void remote_closed();
  void fail(Failure failure);
  std::unique_ptr<rtc::WebSocket> socket_;
  std::mutex socket_operation_mutex_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  Phase phase_ = Phase::connecting;
  Failure failure_ = Failure::none;
  std::deque<std::string> messages_;
  std::size_t queued_bytes_ = 0;
};
} // namespace studio_remote::macos
