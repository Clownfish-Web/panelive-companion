#pragma once

#include <studio_remote/credentials/credential_store.hpp>
#include <studio_remote/discovery/service_discovery.hpp>

#include <string_view>

namespace studio_remote
{
/// Windows production DNS-SD content. The fingerprint is a display hint, never a trust anchor.
ServiceRecord make_companion_service_record(const GatewayIdentity& identity,
                                            std::uint16_t port,
                                            std::string_view installed_providers);

/// Validates without native registration or mutation. Returns the type without its local domain,
/// for adapters whose SDK accepts registration type and domain separately.
std::string validate_dns_sd_record(const ServiceRecord& record);
} // namespace studio_remote
