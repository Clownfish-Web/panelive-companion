#pragma once

#include "media_session_reset_registry.hpp"

#include <nlohmann/json.hpp>

namespace studio_remote
{
using json = nlohmann::json;

json media_session_reset_required_message(const MediaSessionReset& reset);
} // namespace studio_remote
