#pragma once

#include <cstdint>
#include <memory>
#include <studio_remote/discovery/service_discovery.hpp>

namespace studio_remote::macos
{
enum class DiscoveryState
{
  stopped,
  registering,
  advertised,
  failed
};
struct DiscoveryStatus
{
  DiscoveryState state = DiscoveryState::stopped;
  std::int32_t native_error = 0;
};

/// Owns a Bonjour registration and serial dispatch queue. Destruction drains/cancels delivery.
/// Lifecycle calls block briefly on the queue and belong on the configuration worker.
class MacosDnsSd final : public IServiceDiscovery
{
public:
  /// Zero selects all native interfaces; a specific index restricts publication to that interface.
  explicit MacosDnsSd(std::uint32_t interface_index = 0);
  ~MacosDnsSd() override;
  void advertise(const ServiceRecord& record) override;
  void stop() override;
  DiscoveryStatus status() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::macos
