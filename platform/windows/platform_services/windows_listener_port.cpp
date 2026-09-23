#include "windows_listener_port.hpp"

#include <WS2tcpip.h>
#include <WinSock2.h>

#include <stdexcept>

namespace studio_remote::windows
{
namespace
{
class WinsockLifetime
{
public:
  WinsockLifetime()
  {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
      throw std::runtime_error("Windows networking could not be initialized");
  }

  ~WinsockLifetime()
  {
    WSACleanup();
  }
};

class SocketHandle
{
public:
  explicit SocketHandle(SOCKET socket) : socket_(socket)
  {
  }
  ~SocketHandle()
  {
    if (socket_ != INVALID_SOCKET)
      closesocket(socket_);
  }

  SocketHandle(const SocketHandle&) = delete;
  SocketHandle& operator=(const SocketHandle&) = delete;

  SOCKET get() const noexcept
  {
    return socket_;
  }

private:
  SOCKET socket_ = INVALID_SOCKET;
};

std::string listener_warning(std::uint16_t port, std::uint32_t error_code)
{
  const auto port_text = std::to_string(port);
  if (error_code == WSAEACCES)
    return "The Companion cannot use HTTPS/WSS port " + port_text +
           " because Windows denied the bind (socket error 10013). The port may be inside a "
           "dynamic excluded range reserved by Hyper-V, WSL, containers, a VPN, or another "
           "Windows networking component. Choose a different Companion port and apply it.";
  if (error_code == WSAEADDRINUSE)
    return "The Companion cannot use HTTPS/WSS port " + port_text +
           " because another process is already listening on it. Choose a different Companion "
           "port and apply it.";
  return "The Companion cannot use HTTPS/WSS port " + port_text + " (Windows socket error " +
         std::to_string(error_code) + "). Choose a different Companion port and apply it.";
}
} // namespace

ListenerPortAvailability check_listener_port_availability(std::uint16_t port)
{
  if (port == 0)
    return {false, WSAEINVAL, "The Companion HTTPS/WSS port must be between 1 and 65535."};

  try
  {
    WinsockLifetime winsock;
    SocketHandle socket(
        WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED));
    if (socket.get() == INVALID_SOCKET)
    {
      const auto error = static_cast<std::uint32_t>(WSAGetLastError());
      return {false, error, listener_warning(port, error)};
    }

    BOOL exclusive = TRUE;
    if (setsockopt(socket.get(),
                   SOL_SOCKET,
                   SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive),
                   sizeof(exclusive)) == SOCKET_ERROR)
    {
      const auto error = static_cast<std::uint32_t>(WSAGetLastError());
      return {false, error, listener_warning(port, error)};
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
        SOCKET_ERROR)
    {
      const auto error = static_cast<std::uint32_t>(WSAGetLastError());
      return {false, error, listener_warning(port, error)};
    }
    return {true, 0, {}};
  }
  catch (const std::exception& error)
  {
    return {false, 0, error.what()};
  }
}
} // namespace studio_remote::windows
