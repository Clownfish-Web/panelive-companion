#include "client_event_protocol_registry.hpp"

#include <array>
#include <openssl/rand.h>
#include <stdexcept>
#include <utility>

namespace studio_remote
{
std::string new_client_event_id()
{
  std::array<unsigned char, 16> bytes{};
  if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
    throw std::runtime_error("Could not create client event identity");
  std::string scope;
  constexpr std::string_view digits = "0123456789abcdef";
  for (const auto byte : bytes)
  {
    scope += digits[byte >> 4];
    scope += digits[byte & 15];
  }
  return scope;
}
void ClientEventProtocolRegistry::add(std::string message_type, Decoder decoder)
{
  if (message_type.empty() || !decoder ||
      !decoders_.emplace(std::move(message_type), std::move(decoder)).second)
    throw std::invalid_argument("Invalid or duplicate client event protocol registration");
}

std::optional<ClientEventNotification>
ClientEventProtocolRegistry::decode(const json& envelope) const
{
  const auto type = envelope.find("type");
  if (!envelope.is_object() || type == envelope.end() || !type->is_string())
    return std::nullopt;
  const auto found = decoders_.find(type->get_ref<const std::string&>());
  if (found == decoders_.end())
    return std::nullopt;
  return found->second(envelope);
}

bool ClientEventProtocolRegistry::recognizes(const json& envelope) const
{
  if (!envelope.is_object())
    return false;
  const auto type = envelope.find("type");
  return type != envelope.end() && type->is_string() &&
         decoders_.contains(type->get_ref<const std::string&>());
}
} // namespace studio_remote
