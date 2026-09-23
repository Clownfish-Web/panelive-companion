#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace studio_remote
{
enum class ObsSocketReadState
{
  message,
  timeout,
  closed
};
struct ObsSocketRead
{
  ObsSocketReadState state = ObsSocketReadState::closed;
  std::string message;
};

/// Message transport only. OBS authentication, requests, events, and reconnect policy belong to
/// the adapter above this interface. One receiver/connector is allowed; close may cancel it.
class IObsWebSocket
{
public:
  virtual ~IObsWebSocket() = default;
  virtual bool
  connect(std::string_view host, std::uint16_t port, std::chrono::milliseconds timeout) = 0;
  virtual bool send(std::string message) = 0;
  virtual ObsSocketRead receive(std::chrono::milliseconds timeout) = 0;
  virtual void close() = 0;
  virtual std::string last_error() const = 0;
};
} // namespace studio_remote
