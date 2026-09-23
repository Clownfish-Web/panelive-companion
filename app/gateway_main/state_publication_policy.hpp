#pragma once

#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace studio_remote
{

enum class StatePublicationScope
{
  full_replacement_set,
  buses_only,
  outputs_only,
  audio_inputs_only,
  composition_layers_only,
  media_state_only,
  streaming_destinations_only,
};

inline std::optional<std::string> state_publication_composition_id(const nlohmann::json& changed)
{
  if (changed.empty() || !changed.is_object())
    return std::nullopt;
  std::optional<std::string> composition_id;
  for (const auto& [path, value] : changed.items())
  {
    const std::string_view state_path(path);
    constexpr std::string_view layers_prefix = "compositionLayers.";
    constexpr std::string_view metadata_prefix = "compositionMetadata.";
    const auto prefix = state_path.starts_with(layers_prefix)     ? layers_prefix
                        : state_path.starts_with(metadata_prefix) ? metadata_prefix
                                                                  : std::string_view{};
    if (prefix.empty() || !value.is_boolean() || !value.get<bool>())
      return std::nullopt;
    const auto id = std::string(state_path.substr(prefix.size()));
    if (id.empty() || (composition_id && *composition_id != id))
      return std::nullopt;
    composition_id = id;
  }
  return composition_id;
}

inline StatePublicationScope state_publication_scope(const nlohmann::json& changed)
{
  if (changed.empty() || !changed.is_object())
    return StatePublicationScope::full_replacement_set;

  if (state_publication_composition_id(changed))
    return StatePublicationScope::composition_layers_only;

  const bool buses_only = std::all_of(changed.items().begin(),
                                      changed.items().end(),
                                      [](const auto& entry)
                                      {
                                        return entry.key() == "sceneState.programSceneId" ||
                                               entry.key() == "sceneState.previewSceneId";
                                      });
  if (buses_only)
    return StatePublicationScope::buses_only;

  const bool outputs_only = std::all_of(changed.items().begin(),
                                        changed.items().end(),
                                        [](const auto& entry)
                                        {
                                          return entry.key() == "outputs.record" ||
                                                 entry.key() == "outputs.stream" ||
                                                 entry.key() == "streamingDestinations";
                                        });
  if (outputs_only && (changed.contains("outputs.record") || changed.contains("outputs.stream")))
    return StatePublicationScope::outputs_only;

  bool media_state_only = true;
  for (const auto& [path, _] : changed.items())
    media_state_only = media_state_only && std::string_view(path).starts_with("video.");
  if (media_state_only)
    return StatePublicationScope::media_state_only;

  if (changed.size() == 1 && changed.contains("streamingDestinations") &&
      changed["streamingDestinations"].is_boolean() && changed["streamingDestinations"].get<bool>())
    return StatePublicationScope::streaming_destinations_only;

  for (const auto& [path, value] : changed.items())
  {
    const std::string_view state_path(path);
    if (!state_path.starts_with("audioInputs.") || !state_path.ends_with(".routedToMaster") ||
        !value.is_boolean())
      return StatePublicationScope::full_replacement_set;
  }
  return StatePublicationScope::audio_inputs_only;
}

inline nlohmann::json transition_replacement_payload(const nlohmann::json& resource)
{
  return {{"type", "transitionReplacement"},
          {"stateSequence", resource.at("stateSequence")},
          {"currentTransitionId", resource.at("currentTransitionId")},
          {"durationMs", resource.at("durationMs")},
          {"defaultTransition", resource.at("defaultTransition")},
          {"transitions", resource.at("transitions")}};
}

} // namespace studio_remote
