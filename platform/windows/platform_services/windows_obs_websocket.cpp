#include <WS2tcpip.h>
#include <WinSock2.h>

#include "windows_obs_websocket.hpp"
#include "winhttp_obs_socket_connection.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace studio_remote::windows
{
namespace
{
std::wstring endpoint_host(std::string_view host, std::uint16_t port)
{
  if (host.empty() || host.size() > 253 || host.find('\0') != std::string_view::npos || !port)
    throw std::invalid_argument("OBS WebSocket endpoint is invalid");
  const std::string hostname(host);
  IN6_ADDR ipv6{};
  if (InetPtonA(AF_INET6, hostname.c_str(), &ipv6) != 1 &&
      !std::all_of(host.begin(),
                   host.end(),
                   [](char byte)
                   {
                     return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                            (byte >= '0' && byte <= '9') || byte == '.' || byte == '-' ||
                            byte == '_';
                   }))
    throw std::invalid_argument("OBS WebSocket endpoint is invalid");
  return {host.begin(), host.end()};
}
void validate_timeout(std::chrono::milliseconds timeout)
{
  if (timeout < std::chrono::milliseconds::zero() || timeout > std::chrono::hours(24))
    throw std::invalid_argument("OBS WebSocket timeout is outside the supported range");
}
} // namespace

WindowsObsWebSocket::~WindowsObsWebSocket()
{
  close();
}

bool WindowsObsWebSocket::connect(std::string_view host,
                                  std::uint16_t port,
                                  std::chrono::milliseconds timeout)
{
  const auto hostname = endpoint_host(host, port);
  validate_timeout(timeout);
  if (timeout == std::chrono::milliseconds::zero())
    throw std::invalid_argument("OBS WebSocket connection deadline is invalid");
  std::uint64_t revision;
  std::shared_ptr<WinHttpObsSocketConnection> previous;
  {
    std::lock_guard lock(mutex_);
    revision = ++revision_;
    previous = std::move(connection_);
  }
  if (previous)
    previous->close();
  auto current = std::make_shared<WinHttpObsSocketConnection>();
  {
    std::lock_guard lock(mutex_);
    if (revision != revision_)
      return false;
    connection_ = current;
  }
  return current->open(hostname, port, timeout);
}

std::shared_ptr<WinHttpObsSocketConnection> WindowsObsWebSocket::connection() const
{
  std::lock_guard lock(mutex_);
  return connection_;
}

bool WindowsObsWebSocket::send(std::string message)
{
  const auto current = connection();
  return current && current->send(std::move(message));
}

ObsSocketRead WindowsObsWebSocket::receive(std::chrono::milliseconds timeout)
{
  validate_timeout(timeout);
  const auto current = connection();
  return current ? current->receive(timeout) : ObsSocketRead{};
}

void WindowsObsWebSocket::close()
{
  std::shared_ptr<WinHttpObsSocketConnection> current;
  {
    std::lock_guard lock(mutex_);
    ++revision_;
    current = connection_;
  }
  if (current)
    current->close();
}

std::string WindowsObsWebSocket::last_error() const
{
  const auto current = connection();
  return current ? current->last_error() : std::string{};
}
} // namespace studio_remote::windows
