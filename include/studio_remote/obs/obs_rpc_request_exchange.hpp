#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <source_location>
#include <studio_remote/common/types.hpp>
#include <studio_remote/obs/obs_response_mapping.hpp>

namespace studio_remote
{
class ObsDiagnosticEvents;
class ObsPendingRequests;

/// Protocol-boundary input. Views are borrowed only until exchange_obs_rpc_request returns.
struct ObsRpcRequest
{
  std::string_view type;
  std::string_view id;
  json data;
  std::chrono::milliseconds timeout;
  std::source_location caller;
  std::string_view reason;
  std::uint64_t thread_id;
};

/// Synchronously borrowed native operations. Registration must return a valid future from the
/// supplied pending table or throw without inserting an entry. The sender serializes the wire
/// request and returns false or throws on failure. elapsed_ms measures from before requestSent.
/// None of these callbacks is retained, dispatched to another thread or invoked under a table lock.
struct ObsRpcRequestOperations
{
  std::function<std::future<json>(std::string_view)> register_request;
  std::function<bool(std::string_view, std::string_view, json)> send_request;
  std::function<std::uint64_t()> elapsed_ms;
};

/// Windows request sequence shared by native transports. The caller validates connectivity,
/// capacity and deadline before acquiring its native UUID, thread ID and clock origin.
json exchange_obs_rpc_request(ObsPendingRequests& pending,
                              ObsRpcRequest request,
                              const ObsRpcRequestOperations& operations,
                              const ObsDiagnosticEvents* diagnostics = nullptr,
                              const ObsResponseMapping& map_response = {});
} // namespace studio_remote
