#include "gateway_lifecycle.hpp"

#include <utility>

namespace studio_remote
{

void GatewayLifecycle::set_shutdown_handler(ShutdownHandler handler)
{
  std::lock_guard lock(mutex_);
  shutdown_handler_ = std::move(handler);
}

void GatewayLifecycle::observe_obs_connection(bool connected, Clock::time_point observed_at)
{
  std::lock_guard lock(mutex_);
  if (connected)
  {
    obs_has_connected_ = true;
    obs_disconnected_at_.reset();
    return;
  }

  if (obs_has_connected_ && !obs_disconnected_at_)
    obs_disconnected_at_ = observed_at;
}

void GatewayLifecycle::observe_obs_exit_started()
{
  request_shutdown(GatewayShutdownReason::obs_exit_started);
}

void GatewayLifecycle::poll(Clock::time_point now)
{
  ShutdownHandler handler;
  {
    std::lock_guard lock(mutex_);
    if (shutdown_requested_ || !obs_disconnected_at_ ||
        now - *obs_disconnected_at_ < obs_reconnect_timeout)
      return;

    shutdown_requested_ = true;
    handler = shutdown_handler_;
  }

  if (handler)
    handler(GatewayShutdownReason::obs_connection_lost);
}

void GatewayLifecycle::request_shutdown(GatewayShutdownReason reason)
{
  ShutdownHandler handler;
  {
    std::lock_guard lock(mutex_);
    if (!obs_has_connected_ || shutdown_requested_)
      return;

    shutdown_requested_ = true;
    handler = shutdown_handler_;
  }

  if (handler)
    handler(reason);
}

} // namespace studio_remote
