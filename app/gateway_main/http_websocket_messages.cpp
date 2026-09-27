#include "http_server.hpp"

#include <studio_remote/protocol/client_json.hpp>

namespace studio_remote
{
namespace
{
void deliver_rejection(ClientEventConnection& connection, json rejection)
{
  connection.deliver(std::make_shared<const std::string>(rejection.dump()),
                     ClientDeliveryPriority::control);
}
} // namespace

void GatewayHttpServer::handle_websocket_text(mg_connection* native,
                                              ClientEventConnection& connection,
                                              std::string_view text)
{
  // Notifications stay ahead of diagnostic/socket/command locks. The receive thread already
  // charged frame/byte credits, and the bounded parser validates the entire object here.
  auto value = parse_client_json_object(text);
  if (client_event_protocols_.recognizes(value))
  {
    connection.submit(text.size() > maximum_client_event_bytes
                          ? rejected_client_event("event-too-large")
                          : *client_event_protocols_.decode(value));
    return;
  }
  const auto type = value.value("type", "");
  if (endurance_log_ && endurance_log_->enabled())
    endurance_log_->record(
        "websocket", "applicationMessage", {{"type", type}, {"bytes", text.size()}});
  if (type != "resync" && type != "command")
    return;
  std::optional<Device> device;
  {
    std::lock_guard lock(sockets_mutex_);
    if (const auto found = socket_devices_.find(native); found != socket_devices_.end())
      device = found->second;
  }
  if (!device)
    return;
  if (type == "resync")
    handle_websocket_resync(*device, connection);
  else
    handle_websocket_command(*device, connection, std::move(value));
}

void GatewayHttpServer::handle_websocket_resync(const Device& device,
                                                ClientEventConnection& connection)
{
  const auto admission = permit_device(device, RequestScope::websocket_resync);
  if (admission.retry_after_ms)
  {
    deliver_rejection(connection,
                      {{"type", "resyncRejected"},
                       {"error", "rate_limited"},
                       {"scope", "websocket-resync"},
                       {"retryAfterMs", admission.retry_after_ms}});
    return;
  }
  publish_replacements(device);
}

void GatewayHttpServer::handle_websocket_command(const Device& device,
                                                 ClientEventConnection& connection,
                                                 json envelope)
{
  // Admission precedes validation, deduplication and any execution, just as on HTTP POST.
  const auto admission = permit_device(device, RequestScope::control);
  if (admission.retry_after_ms)
  {
    json rejection = {{"status", "rejected"},
                      {"reason", "rate-limited"},
                      {"error", "rate_limited"},
                      {"scope", "controls"},
                      {"retryAfterMs", admission.retry_after_ms}};
    const auto id = envelope.find("commandId");
    if (id != envelope.end() && id->is_string())
    {
      const auto& value = id->get_ref<const std::string&>();
      if (!value.empty() && value.size() <= 128)
        rejection["commandId"] = value;
    }
    deliver_rejection(connection, std::move(rejection));
    return;
  }
  envelope.erase("type");
  const auto outcome = execute_generic_command(device, envelope);
  connection.deliver(std::make_shared<const std::string>(outcome.body.dump()),
                     ClientDeliveryPriority::control);
}
} // namespace studio_remote
