#include "device_role_protocol.hpp"
#include "http_server.hpp"
#include "teleprompter_session_protocol.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace studio_remote
{
namespace
{
std::string next_identification_code(std::uint32_t& sequence,
                                     const std::unordered_map<std::string, std::string>& assigned)
{
  // Two letters give 676 labels for at most 64 ready connections. Skip live labels on wrap.
  std::string code(2, 'A');
  do
  {
    auto value = sequence;
    sequence = (sequence + 1) % (26 * 26);
    for (auto letter = code.rbegin(); letter != code.rend(); ++letter)
    {
      *letter = static_cast<char>('A' + value % 26);
      value /= 26;
    }
  } while (std::ranges::any_of(assigned, [&](const auto& entry) { return entry.second == code; }));
  return code;
}
} // namespace

void GatewayHttpServer::identify_client(std::string_view device_id,
                                        ClientIdentificationOptions options)
{
  if (device_id.empty())
    throw std::invalid_argument("Client identifier is required");
  publish_client_identification(device_id, options);
}

void GatewayHttpServer::identify_all_clients(ClientIdentificationOptions options)
{
  publish_client_identification(std::nullopt, options);
}

std::vector<ClientIdentification> GatewayHttpServer::client_identifications()
{
  std::vector<ClientIdentification> clients;
  for (auto& target : client_identification_targets(std::nullopt))
    if (std::ranges::find(clients, target.client.device_id, &ClientIdentification::device_id) ==
        clients.end())
      clients.push_back(std::move(target.client));
  return clients;
}

void GatewayHttpServer::publish_client_identification(std::optional<std::string_view> device_id,
                                                      ClientIdentificationOptions options)
{
  const auto targets = client_identification_targets(device_id);
  if (targets.empty())
    throw std::runtime_error(device_id ? "Client is offline or not ready to identify"
                                       : "No online clients are ready to identify");
  auto message = json{{"type", "clientIdentify"},
                      {"version", 1},
                      {"eventId", new_client_event_id()},
                      {"durationMs", 3000},
                      {"audible", options.audible},
                      {"vibrate", options.vibrate},
                      {"visual", true}};
  std::vector<LocalClientEventDelivery> deliveries;
  for (const auto& target : targets)
  {
    message["code"] = target.client.code;
    deliveries.push_back({target.connection_id, message.dump()});
  }
  if (!client_events_.publish_local(std::move(deliveries)))
    throw std::runtime_error("Client event queue is full; try identifying again");
}

std::vector<GatewayHttpServer::ClientIdentificationTarget>
GatewayHttpServer::client_identification_targets(std::optional<std::string_view> device_id)
{
  // The registry pins native connection data only while taking the local command snapshot.
  // Publication and all relay/writer work happen outside this cold management lookup lock.
  std::lock_guard lock(sockets_mutex_);
  std::vector<const ClientEventOrigin*> ready;
  for (const auto& [connection, device] : socket_devices_)
  {
    const auto* channel =
        static_cast<ClientEventConnection*>(mg_get_user_connection_data(connection));
    if (channel && channel->ready())
      ready.push_back(&channel->origin());
  }
  std::erase_if(client_identification_codes_,
                [&](const auto& entry)
                {
                  return std::ranges::none_of(
                      ready, [&](const auto* origin) { return origin->device_id == entry.first; });
                });
  std::vector<ClientIdentificationTarget> targets;
  for (const auto* origin : ready)
  {
    auto code = client_identification_codes_.find(origin->device_id);
    if (code == client_identification_codes_.end())
    {
      auto value =
          next_identification_code(client_identification_sequence_, client_identification_codes_);
      code = client_identification_codes_.emplace(origin->device_id, std::move(value)).first;
    }
    if (!device_id || origin->device_id == *device_id)
      targets.push_back({origin->connection_id, {origin->device_id, code->second}});
  }
  return targets;
}

void GatewayHttpServer::publish_teleprompter_session(std::string_view device_id,
                                                     std::string_view session_id,
                                                     bool position_only)
{
  auto* repository = teleprompter_ ? teleprompter_->shared_sessions() : nullptr;
  if (!repository)
    return;
  const auto result = repository->session(device_id, session_id);
  // Membership may have been withdrawn after the accepted HTTP operation.
  if (!result)
  {
    if (result.error != teleprompter::Error::not_found && endurance_log_)
      endurance_log_->record("teleprompter", "snapshotUnavailable");
    return;
  }
  auto snapshot = *result.value;
  struct Recipient
  {
    std::uint64_t connection_id;
    teleprompter::SessionRole role;
  };
  std::vector<Recipient> recipients;
  {
    std::lock_guard lock(sockets_mutex_);
    for (const auto& [connection, device] : socket_devices_)
    {
      const auto* channel =
          static_cast<ClientEventConnection*>(mg_get_user_connection_data(connection));
      if (!channel || !channel->ready() || channel->origin().provider != snapshot.state.provider)
        continue;
      const auto member =
          std::ranges::find(snapshot.members, device.id, &teleprompter::SessionMember::device_id);
      if (member == snapshot.members.end() ||
          (position_only && member->role == teleprompter::SessionRole::master))
        continue;
      recipients.push_back({channel->origin().connection_id, member->role});
    }
  }
  // Master commands and controller samples enter independent writers before monitor deliveries.
  std::ranges::stable_sort(recipients, {}, &Recipient::role);
  std::vector<LocalClientEventDelivery> deliveries;
  for (const auto& recipient : recipients)
  {
    snapshot.role = recipient.role;
    deliveries.push_back({recipient.connection_id, teleprompter_session_event(snapshot).dump()});
  }
  if (!deliveries.empty() && !client_events_.publish_local(std::move(deliveries)) && endurance_log_)
    endurance_log_->record("teleprompter", "eventQueueFull");
}

void GatewayHttpServer::refresh_event_authority(const Device& device) const
{
  auto& session = provider_session(device);
  const auto supported = session.runtime->obs().backend_info().capabilities;
  std::uint32_t permissions = 0;
  if (session.runtime->state().fresh())
  {
    if (supported.transition_execute)
      permissions |= countdown_transition_permission;
    if (supported.recording_start_stop)
      permissions |= countdown_recording_permission;
    if (supported.streaming_start_stop)
      permissions |= countdown_streaming_permission;
  }
  session.event_authority->replace(permissions);
}

bool GatewayHttpServer::open_client_events(mg_connection* connection, const Device& device)
{
  auto& session = provider_session(device);
  refresh_event_authority(device);
  auto* channel = client_events_.connect(
      {++event_connection_sequence_, device.id, session.id, session.event_scope_id},
      device.role >= Role::operator_ ? std::numeric_limits<std::uint32_t>::max() : 0,
      session.event_authority,
      [connection](std::string_view text)
      {
        return mg_websocket_write(connection, MG_WEBSOCKET_OPCODE_TEXT, text.data(), text.size()) >
               0;
      },
      [this, connection](ClientDeliveryFailure reason)
      {
        if (reason != ClientDeliveryFailure::revoked)
          websocket_frames_.transport_error();
        // Fail closed on an undeliverable ordered stream. The peer must reconnect and bootstrap.
        const auto payload = reason == ClientDeliveryFailure::revoked
                                 ? forgotten_device_close_payload()
                                 : std::string("\x03\xf5"
                                               "Client delivery unavailable; reconnect");
        mg_websocket_write(
            connection, MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE, payload.data(), payload.size());
      });
  if (!channel)
  {
    const std::string payload = "\x03\xf5"
                                "Client capacity reached";
    mg_websocket_write(
        connection, MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE, payload.data(), payload.size());
    return false;
  }
  // Role/revocation callbacks use this same cold registry lock. Bind and recheck together so a
  // demotion during bootstrap cannot leave the event lane with its earlier authority snapshot.
  std::lock_guard lock(sockets_mutex_);
  mg_set_user_connection_data(connection, channel);
  const auto current = socket_devices_.find(connection);
  if (current == socket_devices_.end())
  {
    channel->request_revocation_close();
    return false;
  }
  else
    channel->set_permissions(
        current->second.role >= Role::operator_ ? std::numeric_limits<std::uint32_t>::max() : 0);
  return true;
}
} // namespace studio_remote
