#include <studio_remote/streamlabs/streamlabs_state.hpp>

#include <algorithm>

namespace studio_remote::streamlabs
{

std::string_view output_state_name(OutputState state) noexcept
{
  switch (state)
  {
  case OutputState::offline:
    return "offline";
  case OutputState::starting:
    return "starting";
  case OutputState::active:
    return "active";
  case OutputState::stopping:
    return "stopping";
  case OutputState::reconnecting:
    return "reconnecting";
  case OutputState::error:
    return "error";
  case OutputState::unknown:
    return "unknown";
  }
  return "unknown";
}

bool output_state_active(OutputState state) noexcept
{
  return state == OutputState::starting || state == OutputState::active ||
         state == OutputState::reconnecting;
}

const Scene* find_scene(const State& state, std::string_view id)
{
  const auto found = std::find_if(
      state.scenes.begin(), state.scenes.end(), [&](const Scene& scene) { return scene.id == id; });
  return found == state.scenes.end() ? nullptr : &*found;
}

const SceneNode*
find_scene_node(const State& state, std::string_view scene_id, std::string_view node_id)
{
  const auto* scene = find_scene(state, scene_id);
  if (!scene)
    return nullptr;
  const auto found = std::find_if(scene->nodes.begin(),
                                  scene->nodes.end(),
                                  [&](const SceneNode& node) { return node.id == node_id; });
  return found == scene->nodes.end() ? nullptr : &*found;
}

const AudioSource* find_audio_source(const State& state, std::string_view source_id)
{
  const auto found =
      std::find_if(state.audio_sources.begin(),
                   state.audio_sources.end(),
                   [&](const AudioSource& source) { return source.source_id == source_id; });
  return found == state.audio_sources.end() ? nullptr : &*found;
}

} // namespace studio_remote::streamlabs
