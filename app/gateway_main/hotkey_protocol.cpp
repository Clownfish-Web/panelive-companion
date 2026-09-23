#include "hotkey_protocol.hpp"

#include <algorithm>
#include <utility>

namespace studio_remote
{
ParsedHotkeyRequest parse_hotkey_request(const json& body)
{
  if (!body.is_object())
    return {{}, "request-must-be-an-object"};
  const auto request_id = body.find("requestId");
  if (request_id == body.end() || !request_id->is_string() ||
      request_id->get_ref<const std::string&>().empty() ||
      request_id->get_ref<const std::string&>().size() > 128)
    return {{}, "valid-request-id-required"};
  const auto keys = body.find("keys");
  if (keys == body.end() || !keys->is_array() || keys->empty() || keys->size() > 8)
    return {{}, "one-to-eight-keys-required"};

  HotkeyTriggerRequest result;
  result.request_id = request_id->get<std::string>();
  const auto hotkey_id = body.find("hotkeyId");
  if (hotkey_id != body.end())
  {
    if (!hotkey_id->is_string() || hotkey_id->get_ref<const std::string&>().empty() ||
        hotkey_id->get_ref<const std::string&>().size() > 128)
      return {{}, "invalid-hotkey-id"};
    result.hotkey_id = hotkey_id->get<std::string>();
  }
  const auto target = body.find("target");
  if (target != body.end())
  {
    if (!target->is_string())
      return {{}, "invalid-hotkey-target"};
    const auto& target_name = target->get_ref<const std::string&>();
    if (target_name == "system")
      result.target = HotkeyTarget::system;
    else if (target_name != "production")
      return {{}, "invalid-hotkey-target"};
  }
  bool has_non_modifier = false;
  for (const auto& value : *keys)
  {
    if (!value.is_string() || value.get_ref<const std::string&>().empty() ||
        value.get_ref<const std::string&>().size() > 32)
      return {{}, "invalid-key-name"};
    const auto key = parse_hotkey_key(value.get_ref<const std::string&>());
    if (!key)
      return {{}, "unsupported-key-name"};
    if (std::find(result.keys.begin(), result.keys.end(), *key) != result.keys.end())
      return {{}, "duplicate-key"};
    has_non_modifier = has_non_modifier || !is_hotkey_modifier(*key);
    result.keys.push_back(*key);
  }
  if (!has_non_modifier)
    return {{}, "non-modifier-key-required"};
  if (result.target == HotkeyTarget::system && !result.hotkey_id.empty())
    return {{}, "hotkey-id-requires-production-target"};
  return {std::move(result), {}};
}
} // namespace studio_remote
