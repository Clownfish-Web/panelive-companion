#include "macos_obs_websocket.hpp"

#include "macos_obs_socket_connection.hpp"

#include <arpa/inet.h>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace studio_remote::macos
{
namespace
{
std::string endpoint(std::string_view host, std::uint16_t port)
{
  if (host.empty() || host.size() > 253 || !port)
    throw std::invalid_argument("OBS WebSocket endpoint is invalid");
  in6_addr ipv6{};
  std::string hostname(host);
  if (::inet_pton(AF_INET6, hostname.c_str(), &ipv6) == 1)
    hostname = "[" + hostname + "]";
  else
    for (const auto byte : host)
      if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '.' || byte == '-' || byte == '_'))
        throw std::invalid_argument("OBS WebSocket endpoint is invalid");
  return "ws://" + hostname + ":" + std::to_string(port) + "/";
}

} // namespace

class MacosObsWebSocket::Impl
{
public:
  bool connect(std::string_view host, std::uint16_t port, std::chrono::milliseconds timeout)
  {
    const auto url = endpoint(host, port);
    if (timeout <= std::chrono::milliseconds::zero())
      throw std::invalid_argument("OBS WebSocket connection deadline is invalid");
    std::uint64_t revision;
    std::shared_ptr<ObsSocketConnection> previous;
    {
      std::lock_guard lock(mutex_);
      revision = ++revision_;
      previous = std::move(connection_);
    }
    if (previous)
      previous->close();
    auto connection = std::make_shared<ObsSocketConnection>(timeout);
    connection->install_callbacks(connection);
    {
      std::lock_guard lock(mutex_);
      if (revision != revision_)
        return false;
      connection_ = connection;
    }
    return connection->open(url, timeout);
  }

  std::shared_ptr<ObsSocketConnection> connection() const
  {
    std::lock_guard lock(mutex_);
    return connection_;
  }

  void close()
  {
    std::shared_ptr<ObsSocketConnection> current;
    {
      std::lock_guard lock(mutex_);
      ++revision_;
      current = connection_;
    }
    if (current)
      current->close();
  }

private:
  mutable std::mutex mutex_;
  std::uint64_t revision_ = 0;
  std::shared_ptr<ObsSocketConnection> connection_;
};

MacosObsWebSocket::MacosObsWebSocket() : impl_(std::make_unique<Impl>())
{
}
MacosObsWebSocket::~MacosObsWebSocket()
{
  close();
}
bool MacosObsWebSocket::connect(std::string_view host,
                                std::uint16_t port,
                                std::chrono::milliseconds timeout)
{
  return impl_->connect(host, port, timeout);
}
bool MacosObsWebSocket::send(std::string message)
{
  const auto connection = impl_->connection();
  return connection && connection->send(std::move(message));
}
ObsSocketRead MacosObsWebSocket::receive(std::chrono::milliseconds timeout)
{
  const auto connection = impl_->connection();
  return connection ? connection->receive(timeout) : ObsSocketRead{};
}
void MacosObsWebSocket::close()
{
  impl_->close();
}
std::string MacosObsWebSocket::last_error() const
{
  const auto connection = impl_->connection();
  return connection ? connection->last_error() : std::string{};
}
} // namespace studio_remote::macos
