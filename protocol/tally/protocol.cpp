#include <studio_remote/tally/protocol.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <set>
#include <stdexcept>
#include <utility>

namespace studio_remote::remote_tally_protocol
{
namespace
{
using nlohmann::json;
using namespace tally;

constexpr std::array modes{std::pair{"FLOOD", Mode::flood},
                           std::pair{"DOT", Mode::dot},
                           std::pair{"BAR", Mode::bar},
                           std::pair{"OPERATOR", Mode::operator_view}};
constexpr std::array bindings{std::pair{"SCENE", Binding::scene},
                              std::pair{"SOURCE", Binding::source},
                              std::pair{"DOOR", Binding::door},
                              std::pair{"ALL", Binding::all}};
constexpr std::array timers{std::pair{"TIME_IN_STATE", Timer::time_in_state},
                            std::pair{"RECORDING", Timer::recording},
                            std::pair{"TIME_OF_DAY", Timer::time_of_day}};
constexpr std::array states{std::pair{"OFF", State::off},
                            std::pair{"PREVIEW", State::preview},
                            std::pair{"PROGRAM", State::program},
                            std::pair{"RECORDING", State::recording},
                            std::pair{"STREAMING", State::streaming},
                            std::pair{"STANDBY", State::standby},
                            std::pair{"COUNTDOWN", State::countdown},
                            std::pair{"NEXT", State::next},
                            std::pair{"SILENCE", State::silence},
                            std::pair{"LIVE", State::live},
                            std::pair{"DISCONNECTED", State::disconnected},
                            std::pair{"SCENE_UNAVAILABLE", State::scene_unavailable},
                            std::pair{"SOURCE_UNAVAILABLE", State::source_unavailable},
                            std::pair{"SOURCE_DATA_UNAVAILABLE", State::source_data_unavailable}};

template <typename Enum, std::size_t Size>
std::optional<Enum> enum_value(const json& value,
                               const std::array<std::pair<const char*, Enum>, Size>& entries)
{
  if (value.is_string())
  {
    for (const auto& [name, item] : entries)
    {
      if (value.get_ref<const std::string&>() == name)
      {
        return item;
      }
    }
  }
  return std::nullopt;
}

template <typename Enum, std::size_t Size>
const char* enum_name(Enum value, const std::array<std::pair<const char*, Enum>, Size>& entries)
{
  for (const auto& [name, item] : entries)
  {
    if (item == value)
    {
      return name;
    }
  }
  throw std::logic_error("Invalid internal remote tally enum");
}

bool exact_fields(const json& value, std::initializer_list<std::string_view> fields)
{
  if (!value.is_object() || value.size() != fields.size())
  {
    return false;
  }
  for (const auto field : fields)
  {
    if (!value.contains(field))
    {
      return false;
    }
  }
  return true;
}

bool bounded_depth(std::string_view body)
{
  // Bound nesting before allocating a JSON tree. Brackets inside strings are not structure.
  int nesting = 0;
  bool quoted = false;
  bool escaped = false;
  for (const char character : body)
  {
    if (quoted)
    {
      if (escaped)
      {
        escaped = false;
      }
      else if (character == '\\')
      {
        escaped = true;
      }
      else if (character == '"')
      {
        quoted = false;
      }
    }
    else if (character == '"')
    {
      quoted = true;
    }
    else if ((character == '{' || character == '[') && ++nesting > 8)
    {
      return false;
    }
    else if (character == '}' || character == ']')
    {
      --nesting;
    }
  }
  return true;
}

std::variant<json, Error> parse_body(std::string_view body)
{
  if (body.size() > maximum_request_bytes)
  {
    return Error{413, "body", "request-too-large"};
  }
  if (!bounded_depth(body))
  {
    return Error{400, "body", "json-depth-limit"};
  }
  bool invalid = false;
  std::vector<std::set<std::string>> object_keys;
  const auto callback = [&](int depth, json::parse_event_t event, json& value)
  {
    if (depth > 8)
    {
      invalid = true;
    }
    if (event == json::parse_event_t::object_start)
    {
      object_keys.emplace_back();
    }
    else if (event == json::parse_event_t::object_end)
    {
      object_keys.pop_back();
    }
    else if (event == json::parse_event_t::key)
    {
      invalid = !object_keys.back().insert(value.get<std::string>()).second || invalid;
    }
    return true;
  };
  auto value = json::parse(body, callback, false);
  if (invalid || value.is_discarded())
  {
    return Error{400, "body", "invalid-json"};
  }
  return value;
}

bool revision(const json& value)
{
  return (value.is_number_unsigned() && value.get<std::uint64_t>() <= maximum_revision) ||
         (value.is_number_integer() && !value.is_number_unsigned() &&
          value.get<std::int64_t>() >= 0 &&
          value.get<std::int64_t>() <= static_cast<std::int64_t>(maximum_revision));
}

std::optional<Error>
selections(const json& value, std::vector<Selection>& target, std::string_view field)
{
  if (!value.is_array() || value.size() > 256)
  {
    return Error{422, std::string(field), "invalid-selections"};
  }
  for (const auto& selection : value)
  {
    if (!exact_fields(selection, {"id", "name"}) || !selection["id"].is_string() ||
        !selection["name"].is_string())
    {
      return Error{400, std::string(field), "invalid-selection"};
    }
    target.push_back({selection["id"].get<std::string>(), selection["name"].get<std::string>()});
  }
  return std::nullopt;
}

bool operation_fields(const json& value)
{
  return value["operationId"].is_string() &&
         valid_uuid_v4(value["operationId"].get_ref<const std::string&>()) &&
         revision(value["expectedRevision"]);
}
} // namespace

tally::ServiceHealth
parse_health(unsigned status, std::string_view content_type, std::string_view body)
{
  if (status != 200)
    return tally::ServiceHealth::unhealthy;
  std::string media_type;
  for (const char character : content_type)
  {
    if (character != ' ' && character != '\t')
      media_type.push_back(character >= 'A' && character <= 'Z' ? char(character + ('a' - 'A'))
                                                                : character);
  }
  if (body.size() > 4096 ||
      (media_type != "application/json" && media_type != "application/json;charset=utf-8"))
    return tally::ServiceHealth::invalid_response;
  const auto parsed = parse_body(body);
  const auto* value = std::get_if<json>(&parsed);
  if (!value || !exact_fields(*value, {"version", "protocol", "status"}) ||
      !(*value)["version"].is_number_integer() || (*value)["version"] != 1 ||
      (*value)["protocol"] != "remote-tally.v1" || !(*value)["status"].is_string())
    return tally::ServiceHealth::invalid_response;
  if ((*value)["status"] == "healthy")
    return tally::ServiceHealth::healthy;
  return (*value)["status"] == "unhealthy" ? tally::ServiceHealth::unhealthy
                                           : tally::ServiceHealth::invalid_response;
}

bool valid_uuid_v4(std::string_view value)
{
  if (value.size() != 36 || value[14] != '4' ||
      (value[19] != '8' && value[19] != '9' && value[19] != 'a' && value[19] != 'b'))
  {
    return false;
  }
  for (std::size_t index = 0; index < value.size(); ++index)
  {
    const auto character = value[index];
    if (index == 8 || index == 13 || index == 18 || index == 23)
    {
      if (character != '-')
      {
        return false;
      }
    }
    else if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
    {
      return false;
    }
  }
  return true;
}

std::variant<Definition, Error> parse_definition(const json& body)
{
  if (!exact_fields(body,
                    {"id",
                     "label",
                     "mode",
                     "binding",
                     "scenes",
                     "sources",
                     "labels",
                     "showSecondaryLine",
                     "standby",
                     "advancedCountdowns",
                     "timer"}))
  {
    return Error{400, "definition", "invalid-fields"};
  }
  const auto mode = enum_value(body["mode"], modes);
  const auto binding = enum_value(body["binding"], bindings);
  const auto timer = enum_value(body["timer"], timers);
  if (!mode || !binding || !timer || !body["id"].is_string() || !body["label"].is_string() ||
      !body["labels"].is_object() || !body["showSecondaryLine"].is_boolean() ||
      !body["standby"].is_boolean() || !body["advancedCountdowns"].is_boolean())
  {
    return Error{400, "definition", "invalid-field-type-or-enum"};
  }
  Definition result;
  result.id = body["id"].get<std::string>();
  result.label = body["label"].get<std::string>();
  result.mode = *mode;
  result.binding = *binding;
  result.timer = *timer;
  result.show_secondary_line = body["showSecondaryLine"].get<bool>();
  result.standby = body["standby"].get<bool>();
  result.advanced_countdowns = body["advancedCountdowns"].get<bool>();
  if (const auto error = selections(body["scenes"], result.scenes, "scenes"))
  {
    return *error;
  }
  if (const auto error = selections(body["sources"], result.sources, "sources"))
  {
    return *error;
  }
  for (const auto& [name, value] : body["labels"].items())
  {
    const auto state = enum_value(json(name), states);
    if (!state || !value.is_string())
    {
      return Error{400, "labels", "invalid-label"};
    }
    result.labels.emplace(*state, value.get<std::string>());
  }
  if (const auto error = validate(result))
  {
    return Error{422, error->field, error->reason};
  }
  return result;
}

std::variant<PutRequest, Error> parse_put(std::string_view body)
{
  auto parsed = parse_body(body);
  if (const auto* error = std::get_if<Error>(&parsed))
  {
    return *error;
  }
  const auto& value = std::get<json>(parsed);
  if (!exact_fields(
          value, {"version", "operationId", "expectedRevision", "clientProfileId", "definition"}) ||
      !value["version"].is_number_integer() || value["version"] != 1 || !operation_fields(value) ||
      !value["clientProfileId"].is_string() ||
      !valid_text(value["clientProfileId"].get_ref<const std::string&>(), 128))
  {
    return Error{400, "body", "invalid-put"};
  }
  auto definition = parse_definition(value["definition"]);
  if (const auto* error = std::get_if<Error>(&definition))
  {
    return *error;
  }
  return PutRequest{value["operationId"].get<std::string>(),
                    value["expectedRevision"].get<std::uint64_t>(),
                    value["clientProfileId"].get<std::string>(),
                    std::get<Definition>(std::move(definition))};
}

std::variant<MutationRequest, Error> parse_mutation(std::string_view body)
{
  auto parsed = parse_body(body);
  if (const auto* error = std::get_if<Error>(&parsed))
  {
    return *error;
  }
  const auto& value = std::get<json>(parsed);
  if (!exact_fields(value, {"operationId", "expectedRevision"}) || !operation_fields(value))
  {
    return Error{400, "body", "invalid-mutation"};
  }
  return MutationRequest{value["operationId"].get<std::string>(),
                         value["expectedRevision"].get<std::uint64_t>()};
}

json render_config_json(const Definition& definition)
{
  auto labels = json::object();
  for (const auto& [state, label] : definition.labels)
  {
    labels[enum_name(state, states)] = label;
  }
  return {{"label", definition.label},
          {"mode", enum_name(definition.mode, modes)},
          {"binding", enum_name(definition.binding, bindings)},
          {"labels", std::move(labels)},
          {"showSecondaryLine", definition.show_secondary_line},
          {"standby", definition.standby},
          {"advancedCountdowns", definition.advanced_countdowns},
          {"timer", enum_name(definition.timer, timers)}};
}

json definition_json(const Definition& definition)
{
  auto value = render_config_json(definition);
  value["id"] = definition.id;
  value["scenes"] = json::array();
  value["sources"] = json::array();
  for (const auto& selection : definition.scenes)
  {
    value["scenes"].push_back({{"id", selection.id}, {"name", selection.name}});
  }
  for (const auto& selection : definition.sources)
  {
    value["sources"].push_back({{"id", selection.id}, {"name", selection.name}});
  }
  return value;
}
} // namespace studio_remote::remote_tally_protocol
