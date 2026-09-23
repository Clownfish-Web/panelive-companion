#include "http_server.hpp"

#include <map>

namespace studio_remote
{
void GatewayHttpServer::publish_bus_replacements(std::string_view provider)
{
  const auto devices = connected_devices();
  std::map<Role, std::string> states;
  std::optional<std::string> compositions;
  // StateStore invokes this synchronously under its revision lock. Project each role once and
  // publish all tally states before the unchanged composition metadata representation.
  for (const auto& device : devices)
  {
    if (device.production_provider != provider)
      continue;
    auto [state, inserted] = states.try_emplace(device.role);
    if (inserted)
      state->second = state_replacement(device).dump();
    queue_message(state->second, device.id);
  }
  for (const auto& device : devices)
  {
    if (device.production_provider != provider)
      continue;
    if (!compositions)
      compositions = composition_replacement(device).dump();
    queue_message(*compositions, device.id);
  }
}

void GatewayHttpServer::publish_output_replacements(std::string_view provider)
{
  const auto devices = connected_devices();
  std::map<Role, std::string> states;
  std::map<Role, std::string> features;
  ++capability_revision_;
  for (const auto& device : devices)
  {
    if (device.production_provider != provider)
      continue;
    auto [state, inserted] = states.try_emplace(device.role);
    if (inserted)
      state->second = state_replacement(device).dump();
    queue_message(state->second, device.id);
  }
  for (const auto& device : devices)
  {
    if (device.production_provider != provider)
      continue;
    auto [feature, inserted] = features.try_emplace(device.role);
    if (inserted)
    {
      const auto current = capabilities(device);
      feature->second = json{{"type", "capabilityReplacement"},
                             {"contractRevision", 2},
                             {"capabilityRevision", current["capabilityRevision"]},
                             {"features", current["features"]}}
                            .dump();
    }
    queue_message(feature->second, device.id);
    if (const auto replacement = streaming_destination_replacement(device))
      queue_message(replacement->dump(), device.id);
  }
}

void GatewayHttpServer::queue_message(std::string text,
                                      std::optional<std::string> device_id,
                                      std::optional<std::string> production_provider,
                                      ClientDeliveryPriority priority)
{
  std::lock_guard lock(sockets_mutex_);
  queue_message_locked(std::move(text), device_id, production_provider, priority);
}

void GatewayHttpServer::queue_message_locked(std::string text,
                                             const std::optional<std::string>& device_id,
                                             const std::optional<std::string>& production_provider,
                                             ClientDeliveryPriority priority)
{
  if (delivery_stopping_)
    return;
  const auto message = std::make_shared<const std::string>(std::move(text));
  for (const auto& [connection, device] : socket_devices_)
  {
    if ((device_id && device.id != *device_id) ||
        (production_provider && device.production_provider != *production_provider))
      continue;
    if (auto* channel =
            static_cast<ClientEventConnection*>(mg_get_user_connection_data(connection)))
      channel->deliver(message, priority);
  }
}

} // namespace studio_remote
