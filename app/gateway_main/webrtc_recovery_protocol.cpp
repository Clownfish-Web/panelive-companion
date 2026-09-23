#include "webrtc_recovery_protocol.hpp"

#include <algorithm>
#include <string_view>

namespace studio_remote
{
namespace
{
std::string_view string_field(const json& value, const char* name)
{
  const auto field = value.find(name);
  if (field == value.end() || !field->is_string())
    return {};
  return field->get_ref<const std::string&>();
}

bool valid_correlation_id(std::string_view value)
{
  return !value.empty() && value.size() <= 128 &&
         std::all_of(value.begin(),
                     value.end(),
                     [](unsigned char character)
                     {
                       return (character >= 'a' && character <= 'z') ||
                              (character >= 'A' && character <= 'Z') ||
                              (character >= '0' && character <= '9') || character == '-' ||
                              character == '_' || character == '.';
                     });
}
} // namespace

WebRtcRecoveryParseResult parse_webrtc_recovery_request(const json& value)
{
  if (!value.is_object())
    return {std::nullopt, "request-must-be-an-object"};

  const auto recovery_id = string_field(value, "recoveryId");
  if (!valid_correlation_id(recovery_id))
    return {std::nullopt, "invalid-recovery-id"};

  const auto media_session_id = string_field(value, "mediaSessionId");
  if (!valid_correlation_id(media_session_id))
    return {std::nullopt, "invalid-media-session-id"};

  if (string_field(value, "reason") != "initial-video-missing")
    return {std::nullopt, "unsupported-recovery-reason"};
  if (string_field(value, "iceState") != "connected")
    return {std::nullopt, "ice-not-connected"};
  if (!value.contains("telemetryHealthy") || !value["telemetryHealthy"].is_boolean() ||
      !value["telemetryHealthy"].get<bool>())
    return {std::nullopt, "telemetry-not-healthy"};
  if (!value.contains("inboundVideoPackets") || !value["inboundVideoPackets"].is_number_integer() ||
      value["inboundVideoPackets"] != 0)
    return {std::nullopt, "video-packets-already-received"};
  if (!value.contains("framesDecoded") || !value["framesDecoded"].is_number_integer() ||
      value["framesDecoded"] != 0)
    return {std::nullopt, "video-frames-already-decoded"};

  return {WebRtcRecoveryRequest{std::string(recovery_id), std::string(media_session_id)}, {}};
}

} // namespace studio_remote
