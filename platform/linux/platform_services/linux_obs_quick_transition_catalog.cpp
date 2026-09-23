#include "linux_obs_quick_transition_catalog.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace studio_remote::linux
{
namespace
{
std::optional<std::string> scene_collection_file(const std::filesystem::path& root)
{
  std::ifstream input(root / "global.ini");
  std::string line;
  bool basic_section = false;
  while (std::getline(input, line))
  {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.empty() && line.front() == '[' && line.back() == ']')
    {
      basic_section = line == "[Basic]";
      continue;
    }
    constexpr std::string_view key = "SceneCollectionFile=";
    if (basic_section && line.starts_with(key))
      return line.substr(key.size());
  }
  return std::nullopt;
}

bool safe_file_name(std::string_view value)
{
  return !value.empty() && value.size() <= 240 && value != "." && value != ".." &&
         value.find('/') == std::string_view::npos && value.find('\\') == std::string_view::npos;
}

std::optional<std::uint32_t> duration_from_label(std::string_view label)
{
  const auto close = label.rfind(')');
  const auto open = close == std::string_view::npos ? close : label.rfind('(', close);
  if (open == std::string_view::npos || close <= open + 3)
    return std::nullopt;
  const auto value = label.substr(open + 1, close - open - 1);
  if (!value.ends_with("ms"))
    return std::nullopt;
  std::uint32_t duration = 0;
  for (const char digit : value.substr(0, value.size() - 2))
  {
    if (digit < '0' || digit > '9')
      return std::nullopt;
    duration = duration * 10U + static_cast<std::uint32_t>(digit - '0');
  }
  return duration;
}

json* matching_transition(json& transitions, std::string_view label)
{
  json* best = nullptr;
  std::size_t best_size = 0;
  for (auto& transition : transitions.items())
  {
    if (transition.key().size() > best_size &&
        label.find(transition.key()) != std::string_view::npos)
    {
      best = &transition.value();
      best_size = transition.key().size();
    }
  }
  return best;
}
} // namespace

json map_obs_quick_transition_hotkey_catalog(const std::vector<ObservedQuickTransition>& observed,
                                             const json& obs_catalog,
                                             const json& configured)
{
  json by_name = json::object();
  for (const auto& transition : obs_catalog)
  {
    const auto name = transition.value("transitionName", "");
    if (!name.empty() && !by_name.contains(name))
      by_name[name] = transition;
  }
  json result = json::array();
  for (const auto& quick : observed)
  {
    auto* matched = quick.transition_id > 0 ? matching_transition(by_name, quick.label) : nullptr;
    if (!matched)
      continue;
    json transition = *matched;
    const auto kind = transition.value("transitionKind", "");
    const bool fixed = transition.value("transitionFixed", kind == "cut_transition");
    transition["backendTransitionName"] = transition.value("transitionName", "");
    transition["transitionDuration"] =
        fixed                    ? 0U
        : quick.duration_ms != 0 ? quick.duration_ms
                                 : duration_from_label(quick.label)
                                       .value_or(transition.value("transitionDuration", 300U));
    transition["quickTransitionId"] = "obs-quick-transition-" + std::to_string(quick.transition_id);
    transition["quickTransitionNativeId"] = quick.transition_id;
    const auto saved =
        std::find_if(configured.begin(),
                     configured.end(),
                     [&](const json& entry)
                     { return entry.value("quickTransitionNativeId", 0) == quick.transition_id; });
    const bool fade = saved != configured.end() && saved->value("fadeToBlack", false);
    if (fade)
      transition["transitionName"] = "Fade to Black";
    transition["fadeToBlack"] = fade;
    transition["transitionFixed"] = fixed;
    transition["transitionConfigurable"] = transition.value("transitionConfigurable", true);
    result.push_back(std::move(transition));
  }
  return result;
}

json map_obs_quick_transition_catalog(const json& scene_collection, const json& obs_catalog)
{
  const auto quick_transitions = scene_collection.find("quick_transitions");
  if (quick_transitions == scene_collection.end() || !quick_transitions->is_array())
    return json::array();
  std::unordered_map<std::string, json> by_name;
  for (const auto& transition : obs_catalog)
  {
    const auto name = transition.value("transitionName", "");
    if (!name.empty())
      by_name.try_emplace(name, transition);
  }
  json result = json::array();
  for (const auto& quick : *quick_transitions)
  {
    const auto name = quick.value("name", "");
    const auto id = quick.value("id", 0);
    if (name.empty() || id <= 0)
      continue;
    json transition = by_name.contains(name) ? by_name.at(name) : json::object();
    const bool fade = quick.value("fade_to_black", false);
    transition["backendTransitionName"] = name;
    transition["transitionName"] = fade ? "Fade to Black" : name;
    transition["transitionDuration"] = quick.value("duration", 300U);
    transition["quickTransitionId"] = "obs-quick-transition-" + std::to_string(id);
    transition["quickTransitionNativeId"] = id;
    transition["fadeToBlack"] = fade;
    transition["transitionFixed"] = transition.value("transitionFixed", false);
    transition["transitionConfigurable"] = transition.value("transitionConfigurable", true);
    result.push_back(std::move(transition));
  }
  return result;
}

json load_obs_quick_transition_catalog(const std::filesystem::path& root, const json& obs_catalog)
{
  try
  {
    const auto collection = scene_collection_file(root);
    if (!collection || !safe_file_name(*collection))
      return json::array();
    std::ifstream input(root / "basic" / "scenes" / (*collection + ".json"));
    return input ? map_obs_quick_transition_catalog(json::parse(input), obs_catalog)
                 : json::array();
  }
  catch (...)
  {
    return json::array();
  }
}

std::filesystem::path obs_configuration_root()
{
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
    return std::filesystem::path(xdg) / "obs-studio";
  if (const char* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".config" / "obs-studio";
  return {};
}

} // namespace studio_remote::linux
