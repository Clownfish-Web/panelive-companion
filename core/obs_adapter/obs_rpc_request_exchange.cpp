#include <studio_remote/obs/obs_rpc_request_exchange.hpp>

#include <stdexcept>
#include <studio_remote/obs/obs_diagnostic_events.hpp>
#include <studio_remote/obs/obs_pending_requests.hpp>
#include <utility>

namespace studio_remote
{
namespace
{
std::future<json> register_request(const ObsRpcRequest& request,
                                   const ObsRpcRequestOperations& operations,
                                   const ObsDiagnosticEvents* diagnostics)
{
  if (diagnostics)
    diagnostics->request_sent(
        request.type, request.id, request.data, request.caller, request.reason, request.thread_id);
  try
  {
    return operations.register_request(request.id);
  }
  catch (...)
  {
    if (diagnostics)
      diagnostics->request_rejected(
          request.type, request.id, operations.elapsed_ms(), request.thread_id);
    throw;
  }
}

void send_request(ObsPendingRequests& pending,
                  ObsRpcRequest& request,
                  const ObsRpcRequestOperations& operations,
                  const ObsDiagnosticEvents* diagnostics)
{
  try
  {
    if (!operations.send_request(request.type, request.id, std::move(request.data)))
      throw std::runtime_error("Could not send OBS request");
  }
  catch (...)
  {
    pending.erase(request.id);
    if (diagnostics)
      diagnostics->request_send_failed(request.type, request.id, request.thread_id);
    throw;
  }
}
} // namespace

json exchange_obs_rpc_request(ObsPendingRequests& pending,
                              ObsRpcRequest request,
                              const ObsRpcRequestOperations& operations,
                              const ObsDiagnosticEvents* diagnostics,
                              const ObsResponseMapping& map_response)
{
  auto result = register_request(request, operations, diagnostics);
  send_request(pending, request, operations, diagnostics);
  if (result.wait_for(request.timeout) != std::future_status::ready)
  {
    pending.erase(request.id);
    if (diagnostics)
      diagnostics->request_timed_out(
          request.type, request.id, operations.elapsed_ms(), request.thread_id);
    throw std::runtime_error("OBS request timed out");
  }
  try
  {
    auto response = result.get();
    if (diagnostics)
      diagnostics->request_completed(
          request.type, request.id, operations.elapsed_ms(), request.thread_id);
    if (map_response)
      map_response(response);
    return response;
  }
  catch (...)
  {
    pending.erase(request.id);
    if (diagnostics)
      diagnostics->request_rejected(
          request.type, request.id, operations.elapsed_ms(), request.thread_id);
    throw;
  }
}
} // namespace studio_remote
