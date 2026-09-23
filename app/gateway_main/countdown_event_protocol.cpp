#include "countdown_event_protocol.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <set>

namespace studio_remote
{
namespace
{
bool fields(const json& object, std::initializer_list<std::string_view> names)
{
  return object.is_object() && object.size() == names.size() &&
         std::all_of(names.begin(), names.end(), [&](auto name) { return object.contains(name); });
}

bool text_field(const json& value, std::size_t maximum)
{
  if (!value.is_string())
    return false;
  const auto& text = value.get_ref<const std::string&>();
  std::size_t characters = 0;
  bool nonblank = false;
  for (const unsigned char character : text)
  {
    if (character < 0x20 || character == 0x7f)
      return false;
    characters += (character & 0xc0) != 0x80 ? 1 : 0;
    nonblank = nonblank || character != ' ';
  }
  return nonblank && characters <= maximum;
}

bool integer(const json& value)
{
  if (!value.is_number_integer())
    return false;
  if (value.is_number_unsigned())
    return value.get<std::uint64_t>() <=
           static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  return value.get<std::int64_t>() >= 0;
}

bool uuid(std::string_view value)
{
  if (value.size() != 36)
    return false;
  for (std::size_t index = 0; index < value.size(); ++index)
  {
    if (index == 8 || index == 13 || index == 18 || index == 23)
    {
      if (value[index] != '-')
        return false;
    }
    else if (std::string_view("0123456789abcdefABCDEF").find(value[index]) ==
             std::string_view::npos)
      return false;
  }
  return true;
}

bool nullable_text(const json& value, std::size_t maximum, std::optional<std::string>& target)
{
  if (value.is_null())
    return true;
  if (!text_field(value, maximum))
    return false;
  target = value.get<std::string>();
  return true;
}

bool decode_context(const json& value, CountdownContext& context)
{
  if (!fields(value,
              {"profileId",
               "sessionGeneration",
               "previewCompositionId",
               "previewCompositionName",
               "programCompositionId",
               "observedTransitionSequence",
               "streamingTargetIds"}) ||
      !text_field(value.at("profileId"), 128) || !integer(value.at("sessionGeneration")))
    return false;
  context.profile_id = value.at("profileId").get<std::string>();
  context.session_generation = value.at("sessionGeneration").get<std::uint64_t>();
  if (!nullable_text(value.at("previewCompositionId"), 128, context.preview_composition_id) ||
      !nullable_text(value.at("previewCompositionName"), 256, context.preview_composition_name) ||
      !nullable_text(value.at("programCompositionId"), 128, context.program_composition_id))
    return false;
  const auto& sequence = value.at("observedTransitionSequence");
  if (!sequence.is_null())
  {
    if (!integer(sequence))
      return false;
    context.observed_transition_sequence = sequence.get<std::uint64_t>();
  }
  const auto& targets = value.at("streamingTargetIds");
  if (!targets.is_array() || targets.size() > 64)
    return false;
  std::set<std::string> unique;
  for (const auto& target : targets)
  {
    if (!text_field(target, 128) || !unique.insert(target.get<std::string>()).second)
      return false;
    context.streaming_target_ids.push_back(target.get<std::string>());
  }
  return true;
}

std::string rejection_message(std::string_view reason)
{
  return json{{"type", "clientEventRejected"},
              {"version", 1},
              {"eventType", "countdownEvent"},
              {"reason", reason}}
      .dump();
}

std::string_view rejection_reason(CountdownAdmission admission)
{
  switch (admission)
  {
  case CountdownAdmission::unauthorized:
    return "unauthorized";
  case CountdownAdmission::unknown_countdown:
    return "unknown-countdown";
  case CountdownAdmission::context_mismatch:
    return "countdown-context-mismatch";
  case CountdownAdmission::terminal:
    return "countdown-terminal";
  case CountdownAdmission::expired:
    return "countdown-expired";
  case CountdownAdmission::rate_limited:
    return "rate-limited";
  case CountdownAdmission::capacity_exceeded:
    return "event-capacity-exceeded";
  default:
    return "invalid-event";
  }
}
} // namespace

std::optional<CountdownEvent> decode_countdown_event(const json& envelope)
{
  if (!fields(envelope, {"type", "version", "event"}) || envelope.at("type") != "countdownEvent" ||
      !integer(envelope.at("version")) || envelope.at("version") != 1)
    return std::nullopt;
  const auto& value = envelope.at("event");
  if (!fields(value,
              {"eventId",
               "countdownId",
               "stage",
               "operation",
               "durationSeconds",
               "recording",
               "streaming",
               "context",
               "reason"}) ||
      !text_field(value.at("eventId"), 128) || !text_field(value.at("countdownId"), 128) ||
      !value.at("recording").is_boolean() || !value.at("streaming").is_boolean() ||
      !integer(value.at("durationSeconds")) || value.at("durationSeconds") != 10)
    return std::nullopt;
  CountdownEvent event;
  event.event_id = value.at("eventId").get<std::string>();
  event.countdown_id = value.at("countdownId").get<std::string>();
  if (!uuid(event.countdown_id))
    return std::nullopt;
  const auto& stage = value.at("stage");
  if (stage == "started")
    event.stage = CountdownStage::started;
  else if (stage == "go")
    event.stage = CountdownStage::go;
  else if (stage == "cancelled")
    event.stage = CountdownStage::cancelled;
  else
    return std::nullopt;
  if (event.event_id != event.countdown_id + ":" + stage.get<std::string>())
    return std::nullopt;
  const auto& operation = value.at("operation");
  if (operation == "studioTransition")
    event.operation = CountdownOperation::studio_transition;
  else if (operation == "startOutputs")
    event.operation = CountdownOperation::start_outputs;
  else
    return std::nullopt;
  event.recording = value.at("recording").get<bool>();
  event.streaming = value.at("streaming").get<bool>();
  if (event.operation == CountdownOperation::studio_transition
          ? event.recording || event.streaming
          : !event.recording && !event.streaming)
    return std::nullopt;
  if (!decode_context(value.at("context"), event.context))
    return std::nullopt;
  if (event.stage == CountdownStage::cancelled)
  {
    if (!text_field(value.at("reason"), 256))
      return std::nullopt;
    event.reason = value.at("reason").get<std::string>();
  }
  else if (!value.at("reason").is_null())
    return std::nullopt;
  return event;
}

ClientEventNotification rejected_client_event(std::string reason)
{
  return [message = rejection_message(reason)](
             const ClientEventOrigin&, std::uint32_t, CountdownEventPolicy::Clock::time_point)
  { return ClientEventPublication{std::nullopt, message}; };
}

ClientEventNotification countdown_notification(const json& envelope, CountdownEventPolicy& policy)
{
  const auto event = decode_countdown_event(envelope);
  if (!event)
    return rejected_client_event("invalid-event");
  return [event = *event, wire_event = envelope.at("event"), &policy](
             const ClientEventOrigin& origin,
             std::uint32_t permissions,
             CountdownEventPolicy::Clock::time_point now)
  {
    auto broadcast = json{
        {"type", "countdownBroadcast"},
        {"version", 1},
        {"productionProvider", origin.provider},
        {"adapterScopeId", origin.adapter_scope_id},
        {"originDeviceId", origin.device_id},
        {"event",
         wire_event}}.dump();
    if (broadcast.size() > maximum_client_event_bytes)
      return ClientEventPublication{std::nullopt, rejection_message("event-too-large")};
    const CountdownPermissions allowed{(permissions & countdown_transition_permission) != 0,
                                       (permissions & countdown_recording_permission) != 0,
                                       (permissions & countdown_streaming_permission) != 0};
    const auto admission = policy.accept(origin, event, allowed, now);
    if (admission == CountdownAdmission::duplicate)
      return ClientEventPublication{};
    if (admission != CountdownAdmission::accepted)
      return ClientEventPublication{std::nullopt, rejection_message(rejection_reason(admission))};
    return ClientEventPublication{std::move(broadcast), std::nullopt};
  };
}
} // namespace studio_remote
