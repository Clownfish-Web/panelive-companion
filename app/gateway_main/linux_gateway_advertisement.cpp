#include "linux_gateway_advertisement.hpp"

#include <stdexcept>
#include <studio_remote/discovery/companion_service_record.hpp>

namespace studio_remote::linux
{
std::string pairing_url(const GatewayConfig& config)
{
  if (config.pairing_address.empty())
    throw std::runtime_error("No active private IPv4 LAN address is available for pairing");
  return "https://" + config.pairing_address + ":" + std::to_string(config.port);
}

ServiceRecord dns_sd_service_record(const GatewayConfig& config)
{
  auto record = make_companion_service_record(
      {config.instance_id, config.name, config.certificate.fingerprint},
      config.port,
      "obs,companion-teleprompter");
  // Avahi takes the service type separately from its default local domain.
  record.service_type = validate_dns_sd_record(record);
  return record;
}
} // namespace studio_remote::linux
