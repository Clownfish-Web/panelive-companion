#include "teleprompter_session_protocol.hpp"
#include "teleprompter_wire.hpp"

#include <array>
#include <cmath>
#include <utility>

namespace studio_remote
{
namespace
{
using nlohmann::json;
using namespace teleprompter;
using namespace teleprompter_wire;
constexpr std::string_view root = "/v1/teleprompter/sessions";
constexpr std::string_view prefix = "/v1/teleprompter/sessions/";

struct SessionRoute
{
  std::string_view session_id;
  std::string_view script_id;
  bool command = false;
  bool position = false;
};

std::optional<SessionRoute> route(std::string_view path)
{
  if (!path.starts_with(prefix))
    return std::nullopt;
  const auto rest = path.substr(prefix.size());
  const auto slash = rest.find('/');
  const auto id = rest.substr(0, slash);
  if (!valid_id(id))
    return std::nullopt;
  if (slash == std::string_view::npos)
    return SessionRoute{id, {}, false};
  const auto suffix = rest.substr(slash);
  if (suffix == "/position")
    return SessionRoute{id, {}, false, true};
  if (suffix == "/commands")
    return SessionRoute{id, {}, true};
  if (suffix.starts_with("/scripts/") && valid_id(suffix.substr(9)))
    return SessionRoute{id, suffix.substr(9), false};
  return std::nullopt;
}

TeleprompterResponse failure(Error error)
{
  if (error == Error::not_found)
    return {404, {{"error", "teleprompter-session-not-found"}}};
  if (error == Error::control_denied)
    return {403, {{"error", "teleprompter-control-denied"}}};
  if (error == Error::conflict)
    return {409,
            {{"error", "teleprompter-session-conflict"}, {"requiredAction", "reload-session"}}};
  if (error == Error::limit_reached)
    return {409, {{"error", "teleprompter-limit-reached"}}};
  if (error == Error::invalid_progress || error == Error::invalid_script)
    return {400, {{"error", "invalid-teleprompter-request"}}};
  return {503, {{"error", "teleprompter-unavailable"}}};
}

json settings_json(const PresentationSettings& value)
{
  return {{"wordsPerMinute", value.words_per_minute},
          {"mirrorVertical", value.mirror_vertical},
          {"mirrorHorizontal", value.mirror_horizontal},
          {"fontBasisPoints", value.font_basis_points},
          {"fontFamily", font_family_name(value.font_family)},
          {"startBasisPoints", value.start_basis_points},
          {"contentWidthX", value.content_width_x},
          {"speedTrimPercent", value.speed_trim_percent},
          {"lineSpacingBasisPoints", value.line_spacing_basis_points},
          {"voiceFollowEnabled", value.voice_follow_enabled},
          {"speechLanguage", value.speech_language}};
}

json alignment_json(const std::optional<SessionLineAlignment>& alignment)
{
  if (!alignment)
    return nullptr;
  const auto& value = *alignment;
  return {{"lineNumber", value.line_number},
          {"lineText", value.line_text},
          {"lineStartByteOffset", value.line_start_byte_offset},
          {"lineEndByteOffset", value.line_end_byte_offset},
          {"lowerEdgeLine", value.lower_edge_line},
          {"upperEdgeLine", value.upper_edge_line},
          {"linesPerSecond", value.lines_per_second},
          {"sampledAtMonotonicMs", value.sampled_at_monotonic_ms},
          {"version", value.version}};
}

std::optional<SessionLineAlignment>
parse_alignment(const json& body, std::uint64_t sampled_at, std::uint64_t version)
{
  if (!body.is_object() || body.size() != 7)
    return std::nullopt;
  const auto line = integer(body, "lineNumber");
  const auto start = integer(body, "lineStartByteOffset");
  const auto end = integer(body, "lineEndByteOffset");
  const auto text = body.find("lineText");
  if (!line || *line == 0 || !start || !end || *end < *start || text == body.end() ||
      !text->is_string())
    return std::nullopt;
  SessionLineAlignment result;
  result.line_number = *line;
  result.line_start_byte_offset = *start;
  result.line_end_byte_offset = *end;
  result.line_text = text->get<std::string>();
  result.sampled_at_monotonic_ms = sampled_at;
  result.version = version;
  for (const auto* field : {"lowerEdgeLine", "upperEdgeLine", "linesPerSecond"})
    if (!body.contains(field) || !body.at(field).is_number() ||
        !std::isfinite(body.at(field).get<double>()))
      return std::nullopt;
  result.lower_edge_line = body.at("lowerEdgeLine").get<double>();
  result.upper_edge_line = body.at("upperEdgeLine").get<double>();
  result.lines_per_second = body.at("linesPerSecond").get<double>();
  return result;
}

json voice_follow_json(const VoiceFollowState& value)
{
  return {
      {"phase", voice_follow_phase_name(value.phase)},
      {"reason", value.reason},
      {"linesPerSecond", value.lines_per_second},
      {"upperEdgeLine", value.upper_edge_line},
      {"viewportLines", value.viewport_lines},
      {"startupProgress", value.startup_progress ? json(*value.startup_progress) : json(nullptr)}};
}

std::optional<VoiceFollowState> parse_voice_follow(const json& body)
{
  if (!body.is_object() || body.size() != 6 || !body.contains("startupProgress") ||
      !body.contains("phase") || !body.at("phase").is_string() || !body.contains("reason") ||
      !body.at("reason").is_string())
    return std::nullopt;
  const auto phase = parse_voice_follow_phase(body.at("phase").get_ref<const std::string&>());
  if (!phase)
    return std::nullopt;
  for (const auto* key : {"linesPerSecond", "upperEdgeLine", "viewportLines"})
    if (!body.contains(key) || !body.at(key).is_number())
      return std::nullopt;
  VoiceFollowState result{*phase,
                          body.at("reason").get<std::string>(),
                          body.at("linesPerSecond").get<double>(),
                          body.at("upperEdgeLine").get<double>(),
                          body.at("viewportLines").get<double>()};
  if (!body.at("startupProgress").is_null())
  {
    if (!body.at("startupProgress").is_number())
      return std::nullopt;
    result.startup_progress = body.at("startupProgress").get<double>();
  }
  if (!valid_voice_follow_state(result, *phase != VoiceFollowPhase::off))
    return std::nullopt;
  return result;
}

json timed_pause_json(const std::optional<TimedPauseState>& value)
{
  if (!value)
    return nullptr;
  return {{"sourceByteOffset", value->source_byte_offset},
          {"remainingMs", value->remaining_ms},
          {"upperEdgeLine", value->upper_edge_line},
          {"viewportLines", value->viewport_lines}};
}

std::optional<TimedPauseState> parse_timed_pause(const json& body)
{
  if (!body.is_object() || body.size() != 4)
    return std::nullopt;
  const auto byte = integer(body, "sourceByteOffset");
  if (!byte)
    return std::nullopt;
  for (const auto* key : {"remainingMs", "upperEdgeLine", "viewportLines"})
    if (!body.contains(key) || !body.at(key).is_number())
      return std::nullopt;
  return TimedPauseState{*byte,
                         body.at("remainingMs").get<double>(),
                         body.at("upperEdgeLine").get<double>(),
                         body.at("viewportLines").get<double>()};
}

json snapshot_json(const SessionSnapshot& snapshot)
{
  const auto& state = snapshot.state;
  auto scripts = json::array();
  for (const auto& script : snapshot.scripts)
    scripts.push_back(summary_json(script));
  auto members = json::array();
  std::string master_id;
  std::string controller_id;
  for (const auto& member : snapshot.members)
  {
    members.push_back({{"deviceId", member.device_id}, {"role", session_role_name(member.role)}});
    if (member.role == SessionRole::master)
      master_id = member.device_id;
    else if (member.role == SessionRole::controller)
      controller_id = member.device_id;
  }
  return {{"id", state.id},
          {"ownerDeviceId", state.owner_device_id},
          {"productionProvider", state.provider},
          {"version", state.version},
          {"scriptId", state.script_id},
          {"scriptRevision", state.script_revision},
          {"settings", settings_json(state.settings)},
          {"voiceFollow", voice_follow_json(state.voice_follow)},
          {"timedPause", timed_pause_json(state.timed_pause)},
          {"forceContinue", state.force_continue},
          {"startupBypass", state.startup_bypass},
          {"speedAdjustment",
           {{"epoch", state.speed_adjustment.epoch},
            {"sequence", state.speed_adjustment.sequence},
            {"displacement", state.speed_adjustment.displacement},
            {"expiresAtMonotonicMs", state.speed_adjustment.expires_at_monotonic_ms}}},
          {"state", playback_state_name(state.playback)},
          {"elapsedMs", state.elapsed_ms},
          {"anchorMonotonicMs", state.anchor_monotonic_ms},
          {"clockId", snapshot.clock_id},
          {"serverMonotonicMs", snapshot.server_monotonic_ms},
          {"spokenTextByteOffset", snapshot.spoken_text_byte_offset},
          {"role", session_role_name(snapshot.role)},
          {"masterDeviceId", master_id},
          {"controllerDeviceId", controller_id},
          {"wordPosition", state.word_position},
          {"alignment", alignment_json(state.alignment)},
          {"masterWidthCapacity",
           state.master_width_capacity ? json(*state.master_width_capacity) : json(nullptr)},
          {"positionSequence", state.position_sequence},
          {"seekVersion", state.seek_version},
          {"appliedSeekVersion", state.applied_seek_version},
          {"seekByteOffset", state.seek_byte_offset},
          {"seekLineOffset", state.seek_line_offset},
          {"scripts", std::move(scripts)},
          {"members", std::move(members)}};
}

std::optional<PresentationSettings> parse_settings(const json& body)
{
  if (!body.is_object() || body.size() != 11)
    return std::nullopt;
  if (!body.contains("voiceFollowEnabled") || !body.at("voiceFollowEnabled").is_boolean() ||
      !body.contains("speechLanguage") || !body.at("speechLanguage").is_string())
    return std::nullopt;
  if (!body.contains("fontFamily") || !body.at("fontFamily").is_string())
    return std::nullopt;
  const auto font = parse_font_family(body.at("fontFamily").get<std::string>());
  if (!font)
    return std::nullopt;
  const auto spacing = integer(body, "lineSpacingBasisPoints");
  if (!spacing || *spacing < 10000 || *spacing > 30000)
    return std::nullopt;
  const auto width = body.find("contentWidthX");
  if (width == body.end() || !width->is_number())
    return std::nullopt;
  const auto trim = body.find("speedTrimPercent");
  if (trim == body.end() || !trim->is_number_integer())
    return std::nullopt;
  if (trim->is_number_unsigned() && trim->get<std::uint64_t>() > 100)
    return std::nullopt;
  if (!trim->is_number_unsigned() &&
      (trim->get<std::int64_t>() < -100 || trim->get<std::int64_t>() > 100))
    return std::nullopt;
  constexpr std::array names = {"wordsPerMinute", "fontBasisPoints", "startBasisPoints"};
  std::array<std::uint32_t, 3> values{};
  for (std::size_t index = 0; index < names.size(); ++index)
  {
    const auto value = integer(body, names[index]);
    if (!value || *value > 10000)
      return std::nullopt;
    values[index] = static_cast<std::uint32_t>(*value);
  }
  const auto vertical = body.find("mirrorVertical");
  const auto horizontal = body.find("mirrorHorizontal");
  if (vertical == body.end() || horizontal == body.end() || !vertical->is_boolean() ||
      !horizontal->is_boolean())
    return std::nullopt;
  PresentationSettings result{values[0],
                              vertical->get<bool>(),
                              horizontal->get<bool>(),
                              values[1],
                              values[2],
                              width->get<double>(),
                              trim->get<std::int32_t>(),
                              static_cast<std::uint32_t>(*spacing),
                              body.at("voiceFollowEnabled").get<bool>(),
                              body.at("speechLanguage").get<std::string>(),
                              *font};
  return valid_settings(result) ? std::optional(result) : std::nullopt;
}

std::optional<SessionAction> parse_action(const json& body)
{
  const auto type = body.find("type");
  if (type == body.end() || !type->is_string())
    return std::nullopt;
  if (*type == "play" && body.size() == 3)
    return PlaySession{};
  if (*type == "pause" && body.size() == 3)
    return PauseSession{};
  if (*type == "reset" && body.size() == 3)
    return ResetSession{};
  if (*type == "seek" && (body.size() == 4 || (body.size() == 5 && body.contains("lineOffset"))))
  {
    double offset = 0;
    if (body.contains("lineOffset"))
    {
      if (!body.at("lineOffset").is_number())
        return std::nullopt;
      offset = body.at("lineOffset").get<double>();
      if (!std::isfinite(offset) || offset < 0 || offset > 272144)
        return std::nullopt;
    }
    if (const auto position = integer(body, "spokenTextByteOffset"))
      return SeekSession{*position, offset};
  }
  if (*type == "settings" && body.size() == 4 && body.contains("settings"))
  {
    if (const auto settings = parse_settings(body.at("settings")))
      return SetSessionSettings{*settings};
  }
  if (*type == "force-continue" && body.size() == 4 && body.contains("enabled") &&
      body.at("enabled").is_boolean())
    return ForceContinueSession{body.at("enabled").get<bool>()};
  if (*type == "adjust-speed" && body.size() == 4 && body.contains("speedAdjustment"))
  {
    const auto& value = body.at("speedAdjustment");
    if (!value.is_object() || value.size() != 4)
      return std::nullopt;
    const auto epoch = integer(value, "epoch");
    const auto sequence = integer(value, "sequence");
    const auto sampled = integer(value, "sampledAtMonotonicMs");
    if (!epoch || *epoch == 0 || !sequence || *sequence == 0 || !sampled ||
        !value.contains("displacement") || !value.at("displacement").is_number())
      return std::nullopt;
    const auto displacement = value.at("displacement").get<double>();
    if (!std::isfinite(displacement) || std::abs(displacement) > 1)
      return std::nullopt;
    return AdjustSessionSpeed{*epoch, *sequence, displacement, *sampled};
  }
  if (*type == "select-script" && body.size() == 5)
  {
    const auto id = body.find("scriptId");
    const auto revision = integer(body, "scriptRevision");
    if (id != body.end() && id->is_string() && valid_id(id->get_ref<const std::string&>()) &&
        revision && *revision > 0)
      return SelectSessionScript{id->get<std::string>(), *revision};
  }
  return std::nullopt;
}
} // namespace

bool teleprompter_session_read_path(std::string_view path)
{
  const auto parsed = route(path);
  return path == root || (parsed && !parsed->command && !parsed->position);
}

bool teleprompter_session_command_path(std::string_view path)
{
  const auto parsed = route(path);
  return parsed && parsed->command;
}

TeleprompterResponse teleprompter_session_read(ISessionRepository* repository,
                                               std::string_view device_id,
                                               std::string_view path)
{
  if (!teleprompter_session_read_path(path))
    return failure(Error::not_found);
  if (!repository)
    return failure(Error::storage_unavailable);
  if (path == root)
  {
    const auto result = repository->sessions(device_id);
    if (!result)
      return failure(result.error);
    auto sessions = json::array();
    for (const auto& session : *result.value)
      sessions.push_back(snapshot_json(session));
    return {200, {{"sharedTeleprompterVersion", 3}, {"sessions", std::move(sessions)}}};
  }
  const auto parsed = *route(path);
  if (!parsed.script_id.empty())
  {
    const auto result = repository->session_script(device_id, parsed.session_id, parsed.script_id);
    if (!result)
      return failure(result.error);
    auto script = script_json(*result.value);
    script.erase("delivery"); // Shared reads do not expose or mutate the owner's private telemetry.
    script["wordByteOffsets"] = spoken_word_offsets(result.value->document.spoken_text);
    return {200, {{"sharedTeleprompterVersion", 3}, {"script", std::move(script)}}};
  }
  const auto result = repository->session(device_id, parsed.session_id);
  if (!result)
    return failure(result.error);
  return {200, {{"sharedTeleprompterVersion", 3}, {"session", snapshot_json(*result.value)}}};
}

TeleprompterResponse teleprompter_session_command(ISessionRepository* repository,
                                                  std::string_view device_id,
                                                  std::string_view path,
                                                  const json& body)
{
  if (!teleprompter_session_command_path(path))
    return failure(Error::not_found);
  if (!body.is_object())
    return failure(Error::invalid_progress);
  const auto id = body.find("commandId");
  const auto version = integer(body, "expectedVersion");
  const auto action = parse_action(body);
  if (id == body.end() || !id->is_string() || !valid_id(id->get_ref<const std::string&>()) ||
      !version || *version == 0 || !action)
    return failure(Error::invalid_progress);
  if (!repository)
    return failure(Error::storage_unavailable);
  const SessionCommand command{id->get<std::string>(), *version, *action};
  const auto result = repository->command(device_id, route(path)->session_id, command);
  if (!result)
    return failure(result.error);
  return {200,
          {{"sharedTeleprompterVersion", 3},
           {"commandId", command.command_id},
           {"acceptedVersion", result.value->accepted_version},
           {"duplicate", result.value->duplicate},
           {"session", snapshot_json(result.value->snapshot)}}};
}

bool teleprompter_session_position_path(std::string_view path)
{
  const auto parsed = route(path);
  return parsed && parsed->position;
}

TeleprompterResponse teleprompter_session_position(ISessionRepository* repository,
                                                   std::string_view device_id,
                                                   std::string_view path,
                                                   const json& body)
{
  if (!teleprompter_session_position_path(path))
    return failure(Error::not_found);
  if (!body.is_object() || body.size() != 12 || !body.contains("timedPause"))
    return failure(Error::invalid_progress);
  if (!body.contains("voiceFollow"))
    return failure(Error::invalid_progress);
  const auto voice = parse_voice_follow(body.at("voiceFollow"));
  if (!voice)
    return failure(Error::invalid_progress);
  const auto version = integer(body, "expectedVersion");
  const auto sequence = integer(body, "sequence");
  const auto applied_seek = integer(body, "appliedSeekVersion");
  const auto elapsed = integer(body, "elapsedMs");
  const auto sampled_at = integer(body, "sampledAtMonotonicMs");
  const auto clock = body.find("clockId");
  const auto alignment = body.find("alignment");
  const auto position = body.find("wordPosition");
  const auto capacity = body.find("masterWidthCapacity");
  const auto playback = body.find("state");
  if (!version || *version == 0 || !sequence || *sequence == 0 || !applied_seek || !elapsed ||
      !sampled_at || clock == body.end() || !clock->is_string() ||
      !valid_id(clock->get_ref<const std::string&>()) || alignment == body.end() ||
      capacity == body.end() || !capacity->is_number() || !std::isfinite(capacity->get<double>()) ||
      capacity->get<double>() <= 0.0 || position == body.end() || !position->is_number() ||
      !std::isfinite(position->get<double>()) || position->get<double>() < 0 ||
      playback == body.end() || !playback->is_string())
    return failure(Error::invalid_progress);
  const auto state = parse_playback_state(playback->get_ref<const std::string&>());
  if (!state)
    return failure(Error::invalid_progress);
  if (!repository)
    return failure(Error::storage_unavailable);
  SessionPositionReport report;
  report.voice_follow = *voice;
  if (!body.at("timedPause").is_null())
  {
    report.timed_pause = parse_timed_pause(body.at("timedPause"));
    if (!report.timed_pause)
      return failure(Error::invalid_progress);
  }
  report.expected_version = *version;
  report.sequence = *sequence;
  report.applied_seek_version = *applied_seek;
  report.elapsed_ms = *elapsed;
  report.word_position = position->get<double>();
  report.master_width_capacity = capacity->get<double>();
  report.playback = *state;
  report.clock_id = clock->get<std::string>();
  report.sampled_at_monotonic_ms = *sampled_at;
  if (!alignment->is_null())
  {
    report.alignment = parse_alignment(*alignment, *sampled_at, *version);
    if (!report.alignment)
      return failure(Error::invalid_progress);
  }
  const auto result = repository->publish_position(device_id, route(path)->session_id, report);
  if (!result)
    return failure(result.error);
  return {200, {{"sharedTeleprompterVersion", 3}, {"session", snapshot_json(*result.value)}}};
}

json teleprompter_session_event(const SessionSnapshot& snapshot)
{
  return {{"type", "teleprompterSession"},
          {"sharedTeleprompterVersion", 3},
          {"session", snapshot_json(snapshot)}};
}

json teleprompter_session_capability()
{
  return {{"availability", "available"},
          {"access", "observe"},
          {"version", 3},
          {"listEndpoint", root},
          {"requiredEdition", "full"},
          {"connectionMode", "companion"},
          {"controlAuthorization", "session-membership"},
          {"pollIntervalMs", 5000},
          {"positionIntervalMs", 2000},
          {"voiceFollowVersion", 1},
          {"voicePositionIntervalMs", 250},
          {"voiceTrajectoryValidityMs", 1250},
          {"speechLanguages", json::array({"en-US"})},
          {"alignmentIntervalMs", 10000},
          {"staleAfterMs", 10000},
          {"eventType", "teleprompterSession"},
          {"maxMembers", 8},
          {"maxScripts", 100},
          {"retainedCommands", 256}};
}
} // namespace studio_remote
