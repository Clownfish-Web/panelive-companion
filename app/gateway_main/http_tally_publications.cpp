#include "http_server.hpp"

#include "http_request_policy.hpp"
#include "tally_publication_routes.hpp"

#include <studio_remote/tally/protocol.hpp>

namespace studio_remote
{
namespace
{
std::variant<std::string, remote_tally_protocol::Error> read_tally_body(mg_connection* connection)
{
  const auto* info = mg_get_request_info(connection);
  if (!info || info->content_length < 0)
    return remote_tally_protocol::Error{400, "body", "content-length-required"};
  if (static_cast<std::uint64_t>(info->content_length) >
      remote_tally_protocol::maximum_request_bytes)
    return remote_tally_protocol::Error{413, "body", "request-too-large"};
  std::string body(static_cast<std::size_t>(info->content_length), '\0');
  std::size_t offset = 0;
  while (offset < body.size())
  {
    const int read = mg_read(connection, body.data() + offset, body.size() - offset);
    if (read <= 0)
      return remote_tally_protocol::Error{400, "body", "incomplete-request-body"};
    offset += static_cast<std::size_t>(read);
  }
  return body;
}
} // namespace

bool GatewayHttpServer::handlePut(CivetServer*, mg_connection* connection)
{
  return handle_tally_request(connection, HttpRequestMethod::put);
}

bool GatewayHttpServer::handleDelete(CivetServer*, mg_connection* connection)
{
  return handle_tally_request(connection, HttpRequestMethod::delete_);
}

bool GatewayHttpServer::handle_tally_request(mg_connection* connection, HttpRequestMethod method)
{
  http_requests_.fetch_add(1, std::memory_order_relaxed);
  const auto active = active_http_requests_.fetch_add(1, std::memory_order_relaxed) + 1;
  auto maximum = maximum_active_http_requests_.load(std::memory_order_relaxed);
  while (maximum < active && !maximum_active_http_requests_.compare_exchange_weak(
                                 maximum, active, std::memory_order_relaxed))
  {
  }
  struct RequestGuard
  {
    std::atomic<std::uint64_t>& count;
    ~RequestGuard()
    {
      count.fetch_sub(1, std::memory_order_relaxed);
    }
  } guard{active_http_requests_};
  try
  {
    const auto authorization = authorize(connection, "http");
    if (!authorization.device)
    {
      reply_authentication_failure(connection, authorization.failure_reason);
      return true;
    }
    const auto scope = http_request_scope(method, uri(connection));
    const auto admission = permit_device(*authorization.device, scope);
    if (admission.retry_after_ms)
    {
      reply_rate_limited(connection, request_scope_name(scope), admission.retry_after_ms);
      return true;
    }
    if (scope != RequestScope::remote_tally)
    {
      reply(connection, 404, {{"error", "not-found"}});
      return true;
    }
    deliver_tally_request(connection, *authorization.device, method);
  }
  catch (const std::exception&)
  {
    // Do not reflect parser, credential or storage exception text in responses/diagnostics.
    reply(connection, 503, {{"error", "remote-tally-unavailable"}});
  }
  return true;
}

void GatewayHttpServer::deliver_tally_request(mg_connection* connection,
                                              const Device& device,
                                              HttpRequestMethod method)
{
  if (device.production_provider != "obs")
  {
    reply(
        connection, 422, {{"error", "remote-tally-rejected"}, {"reason", "provider-unsupported"}});
    return;
  }
  if (!remote_tallies_)
  {
    reply(connection, 503, {{"error", "remote-tally-unavailable"}});
    return;
  }
  const auto route = tally_route(uri(connection));
  if (method == HttpRequestMethod::get)
  {
    const auto result = read_tally_publications(*remote_tallies_, device.id, route);
    reply(connection, result.status, result.body, result.retry_after_ms);
    return;
  }
  const auto raw = read_tally_body(connection);
  if (const auto* error = std::get_if<remote_tally_protocol::Error>(&raw))
  {
    reply(connection,
          error->status,
          {{"error", "invalid-remote-tally-request"},
           {"reason", error->reason},
           {"field", error->field}});
    return;
  }
  const auto& body = std::get<std::string>(raw);
  const auto result =
      method == HttpRequestMethod::put
          ? put_tally_publication(*remote_tallies_, device.id, route.publication_id, body)
      : method == HttpRequestMethod::delete_
          ? delete_tally_publication(*remote_tallies_, device.id, route.publication_id, body)
          : rotate_tally_link(*remote_tallies_, device.id, route.publication_id, body);
  if (result.changed_publication)
    publish_tally_publication(device.id, *result.changed_publication);
  reply(connection, result.status, result.body, result.retry_after_ms);
}

void GatewayHttpServer::publish_tally_publication(std::string_view device_id,
                                                  std::string_view publication_id)
{
  if (!remote_tallies_ || delivery_stopping_)
    return;
  const auto result = remote_tallies_->get(device_id, publication_id);
  if (const auto* descriptor = std::get_if<tally::PublicationDescriptor>(&result))
    queue_message(json{{"type", "remoteTallyPublicationChanged"},
                       {"version", 1},
                       {"publication", remote_tally_protocol::descriptor_json(*descriptor)}}
                      .dump(),
                  std::string(device_id),
                  std::string("obs"),
                  ClientDeliveryPriority::state);
}
} // namespace studio_remote
