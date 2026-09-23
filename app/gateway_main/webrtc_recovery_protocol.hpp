#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace studio_remote
{
using json = nlohmann::json;

struct WebRtcRecoveryRequest
{
  std::string recovery_id;
  std::string media_session_id;
};

struct WebRtcRecoveryParseResult
{
  std::optional<WebRtcRecoveryRequest> request;
  std::string reason;
};

WebRtcRecoveryParseResult parse_webrtc_recovery_request(const json& value);

} // namespace studio_remote
