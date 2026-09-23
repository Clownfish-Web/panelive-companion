#include "listener_gateway_controller.hpp"

#include <stdexcept>
#include <utility>

namespace studio_remote
{
ListenerGatewayController::ListenerGatewayController(ListenerGatewayBinding binding,
                                                     std::string initial_listener_ports)
    : binding_(std::move(binding)), status_{false, {}, std::move(initial_listener_ports)}
{
  if (!binding_.check_port || !binding_.listeners || !binding_.running ||
      !binding_.stop_discovery || !binding_.stop_listener || !binding_.configure ||
      !binding_.start_listener || !binding_.advertise || !binding_.report_discovery_failure)
    throw std::invalid_argument("Listener gateway bindings must be complete");
}

void ListenerGatewayController::start(std::uint16_t port)
{
  const auto availability = binding_.check_port(port);
  if (!availability.available)
  {
    record_failure(availability.warning);
    throw std::runtime_error(availability.warning);
  }
  const auto requested_listeners = binding_.listeners(port);
  if (binding_.running())
  {
    binding_.stop_discovery();
    binding_.stop_listener();
  }
  binding_.configure(port, requested_listeners);
  try
  {
    binding_.start_listener();
  }
  catch (const std::exception& error)
  {
    const auto warning = "The Companion could not start its HTTPS/WSS listener on port " +
                         std::to_string(port) + ": " + error.what();
    record_failure(warning);
    throw std::runtime_error(warning);
  }
  {
    std::lock_guard lock(status_mutex_);
    status_.listener_ports = requested_listeners;
    status_.listening = true;
    status_.warning.clear();
    active_port_ = port;
  }
  try
  {
    binding_.advertise(port);
  }
  catch (const std::exception& error)
  {
    binding_.report_discovery_failure(error.what());
  }
}

ListenerGatewayStatus ListenerGatewayController::status() const
{
  std::lock_guard lock(status_mutex_);
  return status_;
}

bool ListenerGatewayController::running_on(std::uint16_t port) const
{
  {
    std::lock_guard lock(status_mutex_);
    if (active_port_ != port)
      return false;
  }
  return binding_.running();
}

void ListenerGatewayController::record_failure(std::string warning)
{
  std::lock_guard lock(status_mutex_);
  status_.listening = false;
  status_.warning = std::move(warning);
}
} // namespace studio_remote
