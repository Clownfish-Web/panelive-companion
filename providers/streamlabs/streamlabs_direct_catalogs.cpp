#include <studio_remote/streamlabs/streamlabs_direct_catalogs.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
#include <studio_remote/input/hotkey.hpp>
#include <unordered_set>

namespace studio_remote::streamlabs
{
namespace
{
std::string text(const json& value, const char* field)
{
  const auto found = value.find(field);
  return found != value.end() && found->is_string() ? found->get<std::string>() : "";
}
std::string required_text(const json& value, const char* field)
{
  const auto result = text(value, field);
  if (result.empty())
    throw std::runtime_error("Streamlabs catalog omitted a required string");
  return result;
}
bool boolean(const json& value, const char* field)
{
  const auto found = value.find(field);
  if (found == value.end() || !found->is_boolean())
    throw std::runtime_error("Streamlabs catalog omitted a required boolean");
  return found->get<bool>();
}
void require_array(const json& value)
{
  if (!value.is_array())
    throw std::runtime_error("Streamlabs catalog is not an array");
}
bool supported_action(std::string_view name)
{
  constexpr std::array names{"TOGGLE_START_RECORDING",
                             "TOGGLE_STOP_RECORDING",
                             "TOGGLE_START_STREAMING",
                             "TOGGLE_STOP_STREAMING",
                             "ENABLE_STUDIO_MODE",
                             "DISABLE_STUDIO_MODE",
                             "TRANSITION_STUDIO_MODE"};
  return std::ranges::find(names, name) != names.end();
}
std::string uppercase(std::string value)
{
  std::ranges::transform(
      value, value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}
} // namespace
CanvasState parse_canvas_state(const json& value)
{
  CanvasState result;
  result.dual_output_enabled = boolean(value, "dualOutputMode");
  result.loading = boolean(value, "isLoading");
  const auto& settings = value.at("videoSettings");
  const auto& active = settings.at("activeDisplays");
  for (const auto* id : {"horizontal", "vertical"})
  {
    const bool enabled = boolean(active, id);
    const auto video = settings.value(id, json::object());
    if (!video.is_object())
      throw std::runtime_error("Invalid Streamlabs canvas video settings");
    const auto dimension = [&](const char* key)
    {
      const auto found = video.find(key);
      if (found == video.end())
        return std::uint32_t{0};
      const auto& v = *found;
      if (!v.is_number_integer() || v.get<std::int64_t>() <= 0 ||
          v.get<std::int64_t>() > 2147483647)
        throw std::runtime_error("Invalid Streamlabs canvas dimension");
      return v.get<std::uint32_t>();
    };
    const bool available = std::string_view(id) == "horizontal"
                               ? !result.dual_output_enabled || (enabled && !result.loading)
                               : result.dual_output_enabled && enabled && !result.loading;
    result.canvases.push_back(
        {id, available, dimension("baseWidth"), dimension("baseHeight"), enabled});
  }
  return result;
}

std::vector<ConfiguredAction> parse_configured_actions(const json& value)
{
  const auto& general = value.at("general");
  require_array(general);
  std::vector<ConfiguredAction> result;
  std::unordered_set<std::string> ids;
  for (const auto& item : general)
  {
    const auto id = text(item, "actionName");
    const auto label = text(item, "description");
    if (!supported_action(id) || label.empty() || ids.contains(id))
      continue;
    ConfiguredAction action{id, label, {}};
    const auto bindings = item.find("bindings");
    if (bindings == item.end() || !bindings->is_array())
      continue;
    for (const auto& binding : *bindings)
    {
      auto key = text(binding, "key");
      const auto modifiers = binding.find("modifiers");
      if (key.empty() || modifiers == binding.end() || !modifiers->is_object())
        continue;
      std::vector<std::string> chord;
      for (const auto& [name, public_name] : std::array<std::pair<const char*, const char*>, 4>{
               {{"ctrl", "CTRL"}, {"alt", "ALT"}, {"shift", "SHIFT"}, {"meta", "WIN"}}})
        if (const auto v = modifiers->find(name);
            v != modifiers->end() && v->is_boolean() && v->get<bool>())
          chord.emplace_back(public_name);
      if (key.starts_with("Key"))
        key.erase(0, 3);
      else if (key.starts_with("Digit"))
        key.erase(0, 5);
      else
        key = uppercase(std::move(key));
      const auto public_key = parse_hotkey_key(key);
      if (!public_key || is_hotkey_modifier(*public_key))
        continue;
      chord.push_back(std::move(key));
      if (std::ranges::find(action.bindings, chord) == action.bindings.end())
        action.bindings.push_back(std::move(chord));
    }
    if (!action.bindings.empty())
    {
      ids.insert(id);
      result.push_back(std::move(action));
    }
  }
  return result;
}

std::vector<Destination> parse_destinations(const json& value)
{
  const auto& settings = value.at("goLiveSettings");
  const auto& platforms = settings.at("platforms");
  const auto& custom = settings.at("customDestinations");
  if (!platforms.is_object())
    throw std::runtime_error("Streamlabs destination catalog unavailable");
  require_array(custom);
  std::vector<Destination> result;
  for (const auto& [platform, item] : platforms.items())
  {
    auto label = platform;
    if (platform == "youtube")
      label = "YouTube";
    else if (platform == "twitter")
      label = "X";
    else if (platform == "tiktok")
      label = "TikTok";
    else if (!label.empty())
      label.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(label.front())));
    result.push_back(
        {"streamlabs-platform-" + platform, label, label, boolean(item, "enabled"), platform});
  }
  for (std::size_t index = 0; index < custom.size(); ++index)
    result.push_back({"streamlabs-custom-" + std::to_string(index),
                      required_text(custom[index], "name"),
                      "",
                      boolean(custom[index], "enabled")});
  return result;
}

std::vector<SceneCollection> parse_collections(const json& value, const SceneCollection& active)
{
  require_array(value);
  std::vector<SceneCollection> result;
  std::unordered_set<std::string> ids;
  for (const auto& item : value)
  {
    SceneCollection collection{required_text(item, "id"), required_text(item, "name")};
    if (!ids.insert(collection.id).second)
      throw std::runtime_error("Streamlabs scene collections unavailable");
    result.push_back(std::move(collection));
  }
  if (!ids.contains(active.id))
    throw std::runtime_error("Streamlabs scene collections unavailable");
  return result;
}
std::unordered_map<std::string, std::string> parse_source_types(const json& value)
{
  require_array(value);
  std::unordered_map<std::string, std::string> result;
  for (const auto& item : value)
    result[required_text(item, "sourceId")] = text(item, "type");
  return result;
}
std::vector<SceneNode>
parse_scene_layers(const json& value,
                   std::string_view scene_id,
                   const std::unordered_map<std::string, std::string>& source_types)
{
  require_array(value);
  std::vector<SceneNode> result;
  for (auto it = value.rbegin(); it != value.rend(); ++it)
  {
    const auto& item = *it;
    const auto canvas = text(item, "display");
    const auto source_id = text(item, "sourceId");
    const auto type = source_types.find(source_id);
    auto name = text(item, "name");
    if (name.empty())
      name = source_id.empty() ? "Scene item" : source_id;
    result.push_back({required_text(item, "sceneItemId"),
                      std::string(scene_id),
                      "",
                      source_id,
                      name,
                      required_text(item, "resourceId"),
                      type == source_types.end() ? "" : type->second,
                      boolean(item, "visible"),
                      canvas});
  }
  return result;
}
bool canvas_allows_control(const std::optional<CanvasState>& state, std::string_view canvas)
{
  if (!state)
    return canvas.empty() || canvas == "horizontal";
  return std::ranges::any_of(state->canvases,
                             [&](const auto& c)
                             { return !state->loading && c.available && c.id == canvas; });
}
} // namespace studio_remote::streamlabs
