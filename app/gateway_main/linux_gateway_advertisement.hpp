#pragma once

#include "../../platform/linux/platform_services/linux_config.hpp"

#include <string>
#include <studio_remote/discovery/service_discovery.hpp>

namespace studio_remote::linux
{
/// HTTPS pairing URL on the selected private LAN address. Throws std::runtime_error when the
/// configuration holds no private IPv4 address, because pairing material must not point at
/// loopback or a non-private interface.
std::string pairing_url(const GatewayConfig& config);

/// DNS-SD record advertising this OBS-only Linux companion to mobile clients.
ServiceRecord dns_sd_service_record(const GatewayConfig& config);
} // namespace studio_remote::linux
