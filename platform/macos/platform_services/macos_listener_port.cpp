#include "macos_listener_port.hpp"

#include <cerrno>
#include <ifaddrs.h>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>
#include <utility>

namespace studio_remote::macos
{
namespace
{
class SocketHandle final
{
public:
  explicit SocketHandle(int descriptor) : descriptor_(descriptor)
  {
  }
  ~SocketHandle()
  {
    if (descriptor_ >= 0 && ::close(descriptor_) != 0)
      syslog(LOG_ERR, "Companion could not close listener preflight socket");
  }
  SocketHandle(const SocketHandle&) = delete;
  SocketHandle& operator=(const SocketHandle&) = delete;
  int get() const noexcept
  {
    return descriptor_;
  }

private:
  int descriptor_;
};

ListenerPortAvailability unavailable(std::uint16_t port, int error)
{
  const auto prefix = "The Companion cannot use HTTPS/WSS port " + std::to_string(port);
  std::string warning;
  if (error == EADDRINUSE)
    warning = prefix + " because another process is already listening on it. Choose a different "
                       "Companion port and apply it.";
  else if (error == EACCES || error == EPERM)
    warning = prefix + " because macOS denied the bind. Choose a different Companion port and "
                       "apply it.";
  else
    warning = prefix + " (macOS socket error " + std::to_string(error) +
              "). Choose a different Companion port and apply it.";
  return {false, static_cast<std::uint32_t>(error), std::move(warning)};
}
ListenerPortAvailability check_address(std::uint16_t port, in_addr interface_address)
{
  SocketHandle socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (socket.get() < 0)
    return unavailable(port, errno);
  // Closed connections may retain TIME_WAIT. BSD address reuse permits their replacement;
  // the listen probe below still rejects another live listener without enabling port sharing.
  const int reuse_address = 1;
  if (::setsockopt(socket.get(), SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address)) !=
      0)
    return unavailable(port, errno);
  sockaddr_in address{};
  address.sin_len = sizeof(address);
  address.sin_family = AF_INET;
  address.sin_addr = interface_address;
  address.sin_port = htons(port);
  if (::bind(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    return unavailable(port, errno);
  if (::listen(socket.get(), 1) != 0)
    return unavailable(port, errno);
  return {true, 0, {}};
}
} // namespace

ListenerPortAvailability check_listener_port_availability(std::uint16_t port)
{
  if (port == 0)
    return {false, EINVAL, "The Companion HTTPS/WSS port must be between 1 and 65535."};
  const auto wildcard = check_address(port, in_addr{htonl(INADDR_ANY)});
  if (!wildcard.available)
    return wildcard;
  ifaddrs* interfaces = nullptr;
  if (::getifaddrs(&interfaces) != 0)
    return unavailable(port, errno);
  const std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> owner(interfaces, freeifaddrs);
  // BSD permits wildcard/specific-address coexistence with SO_REUSEADDR. Probe each
  // local address as well to preserve the Windows exclusive-port preflight policy.
  for (auto* interface = owner.get(); interface; interface = interface->ifa_next)
  {
    if (!interface->ifa_addr || interface->ifa_addr->sa_family != AF_INET)
      continue;
    const auto* address = reinterpret_cast<const sockaddr_in*>(interface->ifa_addr);
    const auto result = check_address(port, address->sin_addr);
    if (!result.available)
      return result;
  }
  return {true, 0, {}};
}
} // namespace studio_remote::macos
