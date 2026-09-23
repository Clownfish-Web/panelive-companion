#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/streamlabs/streamlabs_state.hpp>
#include <unordered_map>

namespace studio_remote::streamlabs
{
// Direct translations of the Android Streamlabs Canvas, Hotkey, Multistream and Collection codecs.
CanvasState parse_canvas_state(const json& value);
std::vector<ConfiguredAction> parse_configured_actions(const json& value);
std::vector<Destination> parse_destinations(const json& value);
std::vector<SceneCollection> parse_collections(const json& value, const SceneCollection& active);
std::unordered_map<std::string, std::string> parse_source_types(const json& value);
std::vector<SceneNode>
parse_scene_layers(const json& value,
                   std::string_view scene_id,
                   const std::unordered_map<std::string, std::string>& source_types);
bool canvas_allows_control(const std::optional<CanvasState>& state, std::string_view canvas);
} // namespace studio_remote::streamlabs
