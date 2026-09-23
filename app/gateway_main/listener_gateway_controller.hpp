#pragma once

#include <studio_remote/discovery/listener_port.hpp>

#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace studio_remote
{
struct ListenerGatewayBinding
{
  std::function<ListenerPortAvailability(std::uint16_t)> check_port;
  std::function<std::string(std::uint16_t)> listeners;
  std::function<bool()> running;
  std::function<void()> stop_discovery;
  std::function<void()> stop_listener;
  std::function<void(std::uint16_t, const std::string&)> configure;
  std::function<void()> start_listener;
  std::function<void(std::uint16_t)> advertise;
  std::function<void(std::string_view)> report_discovery_failure;
};

struct ListenerGatewayStatus
{
  bool listening = false;
  std::string warning;
  std::string listener_ports;
};

/// Windows listener startup/recovery sequence. One composition owner serializes start calls;
/// status is safe to read from another thread. Binding targets outlive this controller. The root
/// owns normal shutdown ordering relative to providers, diagnostics, and discovery.
class ListenerGatewayController final
{
public:
  explicit ListenerGatewayController(ListenerGatewayBinding binding,
                                     std::string initial_listener_ports = {});
  void start(std::uint16_t port);
  /// Lifecycle-owner query; an old listener does not satisfy a requested replacement port.
  bool running_on(std::uint16_t port) const;
  ListenerGatewayStatus status() const;

private:
  void record_failure(std::string warning);

  ListenerGatewayBinding binding_;
  mutable std::mutex status_mutex_;
  ListenerGatewayStatus status_;
  std::uint16_t active_port_ = 0;
};
} // namespace studio_remote
