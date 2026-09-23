#pragma once

#include <studio_remote/credentials/audit_store.hpp>
#include <studio_remote/provider/telestrator.hpp>

namespace studio_remote
{
struct TelestratorResponse
{
  int status;
  json body;
};

TelestratorResponse telestrator_response(const Device& device, ITelestratorDiscovery* discovery);
void add_telestrator_capability(json& features,
                                const Device& device,
                                ITelestratorDiscovery* discovery);
} // namespace studio_remote
