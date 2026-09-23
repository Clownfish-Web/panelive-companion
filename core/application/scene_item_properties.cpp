#include "scene_item_properties.hpp"

namespace studio_remote
{

SceneItemProperties::SceneItemProperties(ISceneItemPropertiesSource* source) : source_(source)
{
}

bool SceneItemProperties::refresh()
{
  if (!source_)
    return false;
  auto observation = source_->read();
  if (!observation.available)
    return false;

  std::unordered_map<std::string, std::string> replacement;
  replacement.reserve(observation.values.size());
  for (auto& value : observation.values)
  {
    if (value.container_uuid.empty() || value.scene_item_id <= 0 || value.color.empty())
      continue;
    replacement.insert_or_assign(
        key(value.container_uuid, value.scene_item_id), std::move(value.color));
  }

  std::lock_guard lock(mutex_);
  if (observation.sequence != 0 && observation.sequence == source_sequence_)
    return false;
  source_sequence_ = observation.sequence;
  const bool changed = !observed_ || replacement != colors_;
  observed_ = true;
  colors_ = std::move(replacement);
  return changed;
}

std::optional<std::string> SceneItemProperties::color(std::string_view container_uuid,
                                                      std::int64_t scene_item_id) const
{
  std::lock_guard lock(mutex_);
  const auto found = colors_.find(key(container_uuid, scene_item_id));
  return found == colors_.end() ? std::nullopt : std::optional<std::string>(found->second);
}

json SceneItemProperties::diagnostics() const
{
  std::lock_guard lock(mutex_);
  return {{"observed", observed_},
          {"sourceSequence", source_sequence_},
          {"coloredItemCount", colors_.size()}};
}

std::string SceneItemProperties::key(std::string_view container_uuid,
                                     std::int64_t scene_item_id)
{
  return std::string(container_uuid) + ":" + std::to_string(scene_item_id);
}

} // namespace studio_remote
