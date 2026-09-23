#include "macos_streamlabs_websocket_channel.hpp"

#include "../obs/macos_obs_socket_connection.hpp"

#include <stdexcept>
#include <studio_remote/streamlabs/streamlabs_protocol.hpp>
#include <utility>

namespace studio_remote::macos
{
using namespace std::chrono_literals;

MacosStreamlabsWebSocketChannel::MacosStreamlabsWebSocketChannel(std::uint16_t port,
                                                                 std::string token,
                                                                 VersionProbe version_probe)
    : port_(port), token_(std::move(token)), version_probe_(std::move(version_probe))
{
  if (!port_ || !version_probe_)
    throw std::invalid_argument("Streamlabs WebSocket configuration is invalid");
}

MacosStreamlabsWebSocketChannel::~MacosStreamlabsWebSocketChannel()
{
  disconnect();
}

void MacosStreamlabsWebSocketChannel::connect()
{
  if (connection_)
    return;
  if (token_.empty() || token_.size() > 4096 || token_.find('\0') != std::string::npos)
    throw std::runtime_error("Streamlabs API token is not configured or is invalid");
  version_ = version_probe_();
  auto candidate = std::make_shared<ObsSocketConnection>(5s);
  candidate->install_callbacks(candidate);
  if (!candidate->open("ws://127.0.0.1:" + std::to_string(port_) + "/api/websocket", 5s))
    throw std::runtime_error("Cannot reach the local Streamlabs WebSocket API");
  connection_ = std::move(candidate);
  try
  {
    // Native authentication is the sole credential-bearing operation. It is never logged or
    // exposed through the provider's public request allowlist. The shared transport starts ordinary
    // RPC IDs at two.
    const json request{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "auth"},
        {"params", {{"resource", "TcpServerService"}, {"args", json::array({token_})}}}};
    write_message(request.dump());
    const auto response = streamlabs::parse_message(read_message(5s));
    if (response.event || response.id != 1 || !response.result.is_boolean() ||
        !response.result.get<bool>())
      throw std::runtime_error("Streamlabs Desktop API token was rejected");
  }
  catch (...)
  {
    disconnect();
    throw;
  }
}

void MacosStreamlabsWebSocketChannel::disconnect() noexcept
{
  if (connection_)
    connection_->close();
  connection_.reset();
}

bool MacosStreamlabsWebSocketChannel::connected() const noexcept
{
  return connection_ != nullptr;
}

std::string MacosStreamlabsWebSocketChannel::desktop_version() const
{
  return version_;
}

void MacosStreamlabsWebSocketChannel::write_message(std::string_view message)
{
  if (!connection_ || message.size() > streamlabs::maximum_message_bytes ||
      !connection_->send(std::string(message)))
    throw std::runtime_error("Streamlabs WebSocket request could not be sent");
}

std::string MacosStreamlabsWebSocketChannel::read_message(std::chrono::milliseconds timeout)
{
  if (!connection_)
    throw std::runtime_error("Streamlabs WebSocket is disconnected");
  auto result = connection_->receive(timeout);
  if (result.state == ObsSocketReadState::timeout)
    throw std::runtime_error("Streamlabs WebSocket request timed out");
  if (result.state != ObsSocketReadState::message)
    throw std::runtime_error("Streamlabs Desktop closed the WebSocket connection");
  if (result.message.size() > streamlabs::maximum_message_bytes)
    throw std::runtime_error("Streamlabs WebSocket frame exceeds the configured limit");
  return std::move(result.message);
}
} // namespace studio_remote::macos
