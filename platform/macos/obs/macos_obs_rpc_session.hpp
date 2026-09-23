#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <studio_remote/obs/obs_diagnostic_events.hpp>
#include <studio_remote/obs/obs_pending_requests.hpp>
#include <studio_remote/obs/obs_response_mapping.hpp>
#include <studio_remote/obs/obs_websocket.hpp>
#include <studio_remote/obs/obs_wire_protocol.hpp>

namespace studio_remote::macos
{
struct ObsSessionRead
{
  ObsSocketReadState state = ObsSocketReadState::closed;
  std::optional<ObsWireEvent> event;
};

/// One authenticated connection attempt. The adapter owns its reader and reconnect loop.
/// identify/read run on that reader; request runs on other threads; close cancels either.
/// Never reuse a closed session. No callbacks or network calls run under the state mutex.
class ObsRpcSession final
{
public:
  explicit ObsRpcSession(std::unique_ptr<IObsWebSocket> socket);
  ~ObsRpcSession();
  std::string identify(std::string_view host, std::uint16_t port, std::string_view password);
  ObsSessionRead read(std::chrono::milliseconds timeout);
  nlohmann::json request(std::string_view type,
                         nlohmann::json data = {},
                         std::chrono::milliseconds timeout = std::chrono::seconds(8),
                         const ObsDiagnosticEvents* diagnostics = nullptr,
                         std::source_location caller = std::source_location::current(),
                         std::string_view reason = {},
                         const ObsResponseMapping& map_response = {});
  void close();

private:
  enum class Phase
  {
    initial,
    identifying,
    ready,
    closed
  };
  std::string handshake_message(std::string_view failure);
  void complete(ObsWireResponse response);
  std::future<nlohmann::json> register_request(std::string_view id);
  std::unique_ptr<IObsWebSocket> socket_;
  std::mutex mutex_;
  Phase phase_ = Phase::initial;
  // Lock order is the session phase mutex above, then the table's internal mutex.
  // The table never calls back into the session.
  ObsPendingRequests pending_;
};
} // namespace studio_remote::macos
