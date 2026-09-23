#pragma once

#include "../../core/application/hotkey_service.hpp"

#include <optional>
#include <string>
#include <studio_remote/common/types.hpp>

namespace studio_remote
{
struct ParsedHotkeyRequest
{
  std::optional<HotkeyTriggerRequest> request;
  std::string reason;
};

ParsedHotkeyRequest parse_hotkey_request(const json& body);
} // namespace studio_remote
