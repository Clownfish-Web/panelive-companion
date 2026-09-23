#pragma once

#include <functional>
#include <memory>
#include <studio_remote/obs/obs_adapter.hpp>
#include <studio_remote/obs/obs_diagnostic_events.hpp>
#include <studio_remote/obs/obs_response_mapping.hpp>
#include <studio_remote/obs/obs_websocket.hpp>

namespace studio_remote::macos
{
/// Owns the OBS reader, connection attempts, and cancellation. Request workers can prolong only
/// their current session. Start/stop/destruction belong to one external lifecycle owner.
/// Callbacks run on the reader, must return promptly, and must not call start/stop/request.
class ObsConnection final
{
public:
  using SocketFactory = std::function<std::unique_ptr<IObsWebSocket>()>;
  ObsConnection(std::string host, std::uint16_t port, std::string password);
  ObsConnection(std::string host, std::uint16_t port, std::string password, SocketFactory sockets);
  ~ObsConnection();
  void start();
  void stop();
  void reconfigure(std::string host, std::uint16_t port, std::string password);
  bool connected() const;
  std::string last_error() const;
  nlohmann::json request(std::string_view type,
                         nlohmann::json data = {},
                         const ObsDiagnosticEvents* diagnostics = nullptr,
                         std::source_location caller = std::source_location::current(),
                         std::string_view reason = {},
                         const ObsResponseMapping& map_response = {});
  void on_event(IObsAdapter::EventCallback callback);
  void on_connection(IObsAdapter::ConnectionCallback callback);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::macos
