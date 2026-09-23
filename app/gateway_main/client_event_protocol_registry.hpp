#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/events/client_event_relay.hpp>

#include <functional>
#include <map>
#include <string>

namespace studio_remote
{
std::string new_client_event_id();
/// Register only before listener startup. Each decoder converts wire JSON into a typed policy
/// closure. Unknown message types retain the ordinary WSS path; arbitrary JSON is never relayed.
class ClientEventProtocolRegistry
{
public:
  using Decoder = std::function<ClientEventNotification(const json&)>;
  void add(std::string message_type, Decoder decoder);
  bool recognizes(const json& envelope) const;
  std::optional<ClientEventNotification> decode(const json& envelope) const;

private:
  std::map<std::string, Decoder, std::less<>> decoders_;
};
} // namespace studio_remote
