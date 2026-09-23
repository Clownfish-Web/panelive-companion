#pragma once

#include <memory>
#include <studio_remote/discovery/service_discovery.hpp>
#include <vector>

namespace studio_remote::linux
{
std::vector<std::string> available_lan_ipv4_addresses();
std::string listener_specification(std::uint16_t port, bool allow_non_private_interfaces);
bool listener_port_available(std::uint16_t port);

class AvahiServiceDiscovery final : public IServiceDiscovery
{
public:
  AvahiServiceDiscovery();
  ~AvahiServiceDiscovery() override;
  void advertise(const ServiceRecord& record) override;
  void stop() override;

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};
} // namespace studio_remote::linux
