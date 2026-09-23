#pragma once

#include <studio_remote/common/types.hpp>

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>

namespace studio_remote
{

enum class GatewayShutdownReason
{
  obs_exit_started,
  obs_connection_lost
};

class GatewayLifecycle
{
public:
  using ShutdownHandler = std::function<void(GatewayShutdownReason)>;
  static constexpr auto obs_reconnect_timeout = std::chrono::minutes(5);

  void set_shutdown_handler(ShutdownHandler handler);
  void observe_obs_connection(bool connected, Clock::time_point observed_at = Clock::now());
  void observe_obs_exit_started();
  void poll(Clock::time_point now = Clock::now());

private:
  void request_shutdown(GatewayShutdownReason reason);

  std::mutex mutex_;
  ShutdownHandler shutdown_handler_;
  std::optional<Clock::time_point> obs_disconnected_at_;
  bool obs_has_connected_ = false;
  bool shutdown_requested_ = false;
};

} // namespace studio_remote
