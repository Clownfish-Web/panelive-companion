#include <studio_remote/obs/obs_quick_transition_catalog.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <unordered_map>

namespace studio_remote
{
namespace
{
std::optional<std::uint32_t> duration_from_label(std::string_view label)
{
  const auto close = label.rfind(')');
  if (close == std::string_view::npos)
    return std::nullopt;
  const auto open = label.rfind('(', close);
  if (open == std::string_view::npos || close <= open + 3)
    return std::nullopt;

  const auto value = label.substr(open + 1, close - open - 1);
  if (!value.ends_with("ms"))
    return std::nullopt;

  std::uint32_t duration = 0;
  const auto digits = value.substr(0, value.size() - 2);
  for (const char digit : digits)
  {
    if (digit < '0' || digit > '9')
      return std::nullopt;
    const auto numeric_digit = static_cast<std::uint32_t>(digit - '0');
    if (duration > (std::numeric_limits<std::uint32_t>::max() - numeric_digit) / 10U)
      return std::nullopt;
    duration = (duration * 10U) + numeric_digit;
  }
  return duration;
}

json* matching_transition_by_label(json& transitions_by_name, std::string_view label)
{
  json* best = nullptr;
  std::size_t best_size = 0;
  for (auto& transition : transitions_by_name.items())
  {
    const auto& name = transition.key();
    if (name.size() <= best_size || label.find(name) == std::string_view::npos)
      continue;
    best = &transition.value();
    best_size = name.size();
  }
  return best;
}

} // namespace

json map_obs_quick_transition_hotkey_catalog(
    const std::vector<ObservedQuickTransition>& quick_transitions,
    const json& obs_transition_catalog,
    const json& configured_quick_transitions)
{
  json transitions_by_name = json::object();
  for (const auto& transition : obs_transition_catalog)
  {
    const auto name = transition.value("transitionName", "");
    if (!name.empty() && !transitions_by_name.contains(name))
      transitions_by_name[name] = transition;
  }

  json result = json::array();
  for (const auto& quick_transition : quick_transitions)
  {
    if (quick_transition.transition_id <= 0 || quick_transition.label.empty())
      continue;

    auto* matched = matching_transition_by_label(transitions_by_name, quick_transition.label);
    if (!matched)
      continue;

    json transition = *matched;
    const auto native_kind = transition.value("transitionKind", "");
    const auto fixed = transition.value("transitionFixed", native_kind == "cut_transition");
    transition["backendTransitionName"] = transition.value("transitionName", "");
    transition["transitionDuration"] =
        fixed ? 0U
        : quick_transition.duration_ms != 0
            ? quick_transition.duration_ms
            : duration_from_label(quick_transition.label)
                  .value_or(transition.value("transitionDuration", 300U));
    transition["quickTransitionId"] =
        "obs-quick-transition-" + std::to_string(quick_transition.transition_id);
    transition["quickTransitionNativeId"] = quick_transition.transition_id;
    const auto configured = std::find_if(
        configured_quick_transitions.begin(),
        configured_quick_transitions.end(),
        [&](const json& entry)
        { return entry.value("quickTransitionNativeId", 0) == quick_transition.transition_id; });
    const bool fade_to_black =
        configured != configured_quick_transitions.end() && configured->value("fadeToBlack", false);
    if (fade_to_black)
      transition["transitionName"] = "Fade to Black";
    transition["fadeToBlack"] = fade_to_black;
    transition["transitionFixed"] = fixed;
    transition["transitionConfigurable"] = transition.value("transitionConfigurable", true);
    result.push_back(std::move(transition));
  }
  return result;
}

json map_obs_quick_transition_catalog(const json& scene_collection,
                                      const json& obs_transition_catalog)
{
  const auto quick_transitions = scene_collection.find("quick_transitions");
  if (quick_transitions == scene_collection.end() || !quick_transitions->is_array())
    return json::array();

  std::unordered_map<std::string, json> transitions_by_name;
  for (const auto& transition : obs_transition_catalog)
  {
    const auto name = transition.value("transitionName", "");
    if (!name.empty())
      transitions_by_name.try_emplace(name, transition);
  }

  json result = json::array();
  for (const auto& quick_transition : *quick_transitions)
  {
    const auto name = quick_transition.value("name", "");
    const auto id = quick_transition.value("id", 0);
    if (name.empty() || id <= 0)
      continue;

    auto transition =
        transitions_by_name.contains(name) ? transitions_by_name.at(name) : json::object();
    transition["backendTransitionName"] = name;
    transition["transitionName"] =
        quick_transition.value("fade_to_black", false) ? "Fade to Black" : name;
    transition["transitionDuration"] = quick_transition.value("duration", 300U);
    transition["quickTransitionId"] = "obs-quick-transition-" + std::to_string(id);
    transition["quickTransitionNativeId"] = id;
    transition["fadeToBlack"] = quick_transition.value("fade_to_black", false);
    transition["transitionFixed"] = transition.value("transitionFixed", false);
    transition["transitionConfigurable"] = transition.value("transitionConfigurable", true);
    result.push_back(std::move(transition));
  }
  return result;
}

} // namespace studio_remote
