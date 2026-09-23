#include "telestrator_protocol.hpp"

namespace studio_remote
{
TelestratorResponse telestrator_response(const Device& device, ITelestratorDiscovery* discovery)
{
  if (device.role < Role::operator_)
    return {403, {{"error", "operator_role_required"}}};
  if (device.production_provider != "vmix" || !discovery)
    return {404, {{"error", "telestrator-unsupported"}}};
  const auto descriptor = discovery->snapshot();
  const bool ready = descriptor.status == TelestratorStatus::ready;
  json body = {{"contractRevision", 2},
               {"productionProvider", "vmix"},
               {"ready", ready},
               {"url", ready ? json(descriptor.url) : json(nullptr)}};
  if (!ready)
    body["reason"] = telestrator_reason(descriptor.status);
  return {200, std::move(body)};
}

void add_telestrator_capability(json& features,
                                const Device& device,
                                ITelestratorDiscovery* discovery)
{
  if (device.role < Role::operator_ || device.production_provider != "vmix" || !discovery)
    return;
  const auto descriptor = discovery->snapshot();
  const bool ready = descriptor.status == TelestratorStatus::ready;
  json capability = {{"access", "control"},
                     {"availability", ready ? "available" : "temporarily-unavailable"}};
  if (!ready)
    capability["reason"] = telestrator_reason(descriptor.status);
  features["view.telestrator.web"] = std::move(capability);
}
} // namespace studio_remote
