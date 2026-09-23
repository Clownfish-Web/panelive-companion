#include "teleprompter_protocol.hpp"
#include "teleprompter_wire.hpp"

#include <algorithm>
#include <array>

namespace studio_remote
{
namespace teleprompter_wire
{
using nlohmann::json;
using namespace teleprompter;
constexpr std::string_view scripts_path = "/v1/teleprompter/scripts";
constexpr std::string_view script_prefix = "/v1/teleprompter/scripts/";
constexpr std::string_view progress_suffix = "/progress";

template <typename Value> json nullable(const std::optional<Value>& value)
{
  return value ? json(*value) : json(nullptr);
}

TeleprompterResponse failure(Error error)
{
  switch (error)
  {
  case Error::not_found:
    return {404, {{"error", "teleprompter-script-not-found"}}};
  case Error::device_unavailable:
    return {401, {{"error", "unauthorized"}, {"reason", "device_not_found_or_revoked"}}};
  case Error::control_denied:
    return {403, {{"error", "teleprompter-control-denied"}}};
  case Error::conflict:
    return {409, {{"error", "teleprompter-conflict"}, {"requiredAction", "reload-script"}}};
  case Error::invalid_progress:
  case Error::invalid_script:
    return {400, {{"error", "invalid-teleprompter-request"}}};
  case Error::limit_reached:
    return {409, {{"error", "teleprompter-limit-reached"}}};
  case Error::storage_unavailable:
  case Error::none:
    return {503, {{"error", "teleprompter-unavailable"}}};
  }
  return {503, {{"error", "teleprompter-unavailable"}}};
}

json summary_json(const Summary& summary)
{
  return {{"id", summary.id},
          {"name", summary.name},
          {"revision", summary.revision},
          {"createdAtUnixMs", summary.created_at_unix_ms},
          {"modifiedAtUnixMs", summary.modified_at_unix_ms},
          {"wordCount", summary.word_count},
          {"estimatedDurationMs", summary.estimated_duration_ms}};
}

json delivery_json(const Delivery& delivery)
{
  json progress = nullptr;
  if (delivery.progress)
  {
    const auto& value = *delivery.progress;
    progress = {{"revision", value.revision},
                {"sequence", value.sequence},
                {"state", playback_state_name(value.state)},
                {"spokenTextByteOffset", value.spoken_text_byte_offset},
                {"elapsedMs", value.elapsed_ms},
                {"cueId", nullable(value.cue_id)}};
  }
  return {{"retrievedRevision", nullable(delivery.retrieved_revision)},
          {"retrievedAtUnixMs", nullable(delivery.retrieved_at_unix_ms)},
          {"progress", std::move(progress)},
          {"receivedAtUnixMs", nullable(delivery.received_at_unix_ms)},
          {"cueReceivedAtUnixMs", nullable(delivery.cue_received_at_unix_ms)}};
}

json script_json(const Script& script)
{
  auto result = summary_json(script.summary);
  result["format"] = "plain-text-cues-v1";
  result["content"] = script.document.draft.content;
  result["spokenText"] = script.document.spoken_text;
  result["wordsPerMinute"] = script.document.draft.words_per_minute;
  auto cues = json::array();
  for (const auto& cue : script.document.cues)
    cues.push_back({{"id", cue.id},
                    {"type", cue.type == CueType::bookmark ? "bookmark" : "pause"},
                    {"label", cue.label},
                    {"spokenTextByteOffset", cue.spoken_text_byte_offset},
                    {"estimatedAtMs", cue.estimated_at_ms},
                    {"pauseMs", cue.pause_ms}});
  result["cues"] = std::move(cues);
  result["delivery"] = delivery_json(script.delivery);
  return result;
}

bool valid_id(std::string_view id)
{
  return !id.empty() && id.size() <= 128 &&
         std::ranges::all_of(id,
                             [](unsigned char value)
                             {
                               return (value >= 'a' && value <= 'z') ||
                                      (value >= 'A' && value <= 'Z') ||
                                      (value >= '0' && value <= '9') || value == '-' ||
                                      value == '_';
                             });
}

std::optional<std::uint64_t> integer(const json& body, std::string_view field)
{
  const auto found = body.find(field);
  if (found == body.end() || !found->is_number_integer())
    return std::nullopt;
  if (found->is_number_unsigned())
  {
    const auto value = found->get<std::uint64_t>();
    return value <= maximum_safe_integer ? std::optional(value) : std::nullopt;
  }
  const auto value = found->get<std::int64_t>();
  if (value < 0 || static_cast<std::uint64_t>(value) > maximum_safe_integer)
    return std::nullopt;
  return static_cast<std::uint64_t>(value);
}

std::optional<Progress> parse_progress(const json& body)
{
  if (!body.is_object() || body.size() != 6)
    return std::nullopt;
  const auto revision = integer(body, "revision");
  const auto sequence = integer(body, "sequence");
  const auto position = integer(body, "spokenTextByteOffset");
  const auto elapsed = integer(body, "elapsedMs");
  const auto state_field = body.find("state");
  const auto cue = body.find("cueId");
  if (!revision || !sequence || !position || !elapsed || state_field == body.end() ||
      !state_field->is_string() || cue == body.end() || !(cue->is_null() || cue->is_string()))
    return std::nullopt;
  const auto state = parse_playback_state(state_field->get_ref<const std::string&>());
  if (!state || *revision == 0 || *sequence == 0 || *elapsed > 86'400'000 ||
      (!cue->is_null() && (!valid_id(cue->get_ref<const std::string&>()))))
    return std::nullopt;
  return Progress{*revision,
                  *sequence,
                  *state,
                  *position,
                  *elapsed,
                  cue->is_null() ? std::nullopt : std::optional(cue->get<std::string>())};
}
} // namespace teleprompter_wire

using namespace teleprompter_wire;

bool teleprompter_read_path(std::string_view path)
{
  return path == scripts_path ||
         (path.starts_with(script_prefix) && valid_id(path.substr(script_prefix.size())));
}

bool teleprompter_progress_path(std::string_view path)
{
  if (!path.starts_with(script_prefix) || !path.ends_with(progress_suffix) ||
      path.size() <= script_prefix.size() + progress_suffix.size())
    return false;
  return valid_id(path.substr(script_prefix.size(),
                              path.size() - script_prefix.size() - progress_suffix.size()));
}

TeleprompterResponse
teleprompter_read(IRepository* repository, std::string_view device_id, std::string_view path)
{
  if (!teleprompter_read_path(path))
    return failure(Error::not_found);
  if (!repository)
    return failure(Error::storage_unavailable);
  if (path == scripts_path)
  {
    const auto listed = repository->list(device_id);
    if (!listed)
      return failure(listed.error);
    auto scripts = json::array();
    for (const auto& summary : *listed.value)
      scripts.push_back(summary_json(summary));
    return {200, {{"teleprompterVersion", 1}, {"scripts", std::move(scripts)}}};
  }
  const auto script = repository->retrieve(device_id, path.substr(script_prefix.size()));
  if (!script)
    return failure(script.error);
  return {200, {{"teleprompterVersion", 1}, {"script", script_json(*script.value)}}};
}

TeleprompterResponse teleprompter_report(IRepository* repository,
                                         std::string_view device_id,
                                         std::string_view path,
                                         const json& body)
{
  if (!teleprompter_progress_path(path))
    return failure(Error::not_found);
  const auto progress = parse_progress(body);
  if (!progress)
    return failure(Error::invalid_progress);
  if (!repository)
    return failure(Error::storage_unavailable);
  const auto id = path.substr(script_prefix.size(),
                              path.size() - script_prefix.size() - progress_suffix.size());
  const auto delivery = repository->report_progress(device_id, id, *progress);
  if (!delivery)
    return failure(delivery.error);
  return {200, {{"teleprompterVersion", 1}, {"delivery", delivery_json(*delivery.value)}}};
}
} // namespace studio_remote
