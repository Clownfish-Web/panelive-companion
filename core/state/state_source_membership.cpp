#include <studio_remote/state/state_store.hpp>

#include <utility>

namespace studio_remote
{
json StateStore::apply_scene_visibility_locked(const json& data)
{
  ++source_observation_generation_;
  const auto container_id = data.value("sceneUuid", "");
  const auto item_id = data.value("sceneItemId", 0LL);
  const bool enabled = data.value("sceneItemEnabled", false);
  bool found =
      backend_id_ == "obs" && source_membership_.set_enabled(container_id, item_id, enabled);
  json changed = json::object();
  if (backend_id_ == "obs")
    changed["sourceMembership"] = true;
  const auto matches = [&](const std::string& id)
  { return !container_id.empty() && id == container_id; };
  const auto update = [&](const std::string& scene_id, std::vector<SceneItem>& items)
  {
    for (auto& item : items)
      if (item.id == item_id)
      {
        item.enabled = enabled;
        found = true;
        changed["sceneItems." + scene_id + "." + std::to_string(item_id) + ".enabled"] = enabled;
      }
  };
  for (auto& [scene_id, scene] : scenes_)
  {
    auto& roots = items_[scene_id];
    if (matches(scene.backend_id))
      update(scene_id, roots);
    for (const auto& group : roots)
      if (group.group && matches(group.source_id))
      {
        const auto children = group_items_.find(scene_id + ":" + std::to_string(group.id));
        if (children != group_items_.end())
          update(scene_id, children->second);
      }
    // Composed thumbnails of ancestors can change even when their own eyes do not.
    if (backend_id_ == "obs" || matches(scene.backend_id))
      ++scene.item_revision;
  }
  if (!found)
  {
    fresh_ = false;
    changed["resyncRequired"] = true;
  }
  return changed;
}

std::vector<SourceOccurrence> StateStore::read_source_occurrences(const json& values,
                                                                  const std::string& container_id,
                                                                  std::size_t depth) const
{
  std::vector<SourceOccurrence> result;
  if (depth >= 64 || !values.is_array())
  {
    SourceOccurrence unavailable;
    unavailable.available = false;
    result.push_back(std::move(unavailable));
    return result;
  }
  for (const auto& value : values)
  {
    SourceOccurrence occurrence;
    occurrence.name = value.value("sourceName", "");
    occurrence.resource_id = value.value("sourceUuid", "");
    occurrence.container_id = container_id;
    occurrence.item_id = value.value("sceneItemId", 0LL);
    occurrence.enabled = value.value("sceneItemEnabled", false);
    const auto source_type = value.value("sourceType", "");
    occurrence.available =
        value.value("visibilityStateAvailable", true) && value.contains("sceneItemEnabled") &&
        (source_type == "OBS_SOURCE_TYPE_INPUT" || source_type == "OBS_SOURCE_TYPE_SCENE");
    if (value.value("isGroup", false))
    {
      const auto children = value.find("groupSceneItems");
      occurrence.available =
          occurrence.available && children != value.end() && children->is_array();
      if (children != value.end())
        occurrence.children = read_source_occurrences(*children, occurrence.resource_id, depth + 1);
    }
    else if (source_type == "OBS_SOURCE_TYPE_SCENE")
      occurrence.referenced_composition_id = occurrence.resource_id;
    result.push_back(std::move(occurrence));
  }
  return result;
}

json StateStore::source_membership_locked(std::string_view composition_id) const
{
  auto membership = source_membership_.resolve(composition_id);
  membership.available = membership.available && backend_id_ == "obs" && fresh_ && obs_connected_;
  json resources = json::array();
  for (const auto& resource : membership.resources)
    if (!resource.resource_id.empty())
      resources.push_back(
          {{"resourceId", resource.resource_id},
           {"name", resource.name},
           {"active", membership.available ? json(resource.active) : json(nullptr)}});
  return {{"available", membership.available}, {"resources", std::move(resources)}};
}
} // namespace studio_remote
