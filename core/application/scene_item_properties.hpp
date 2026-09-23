#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/scene/scene_item_properties_source.hpp>

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace studio_remote
{

class SceneItemProperties
{
public:
  explicit SceneItemProperties(ISceneItemPropertiesSource* source = nullptr);

  bool refresh();
  std::optional<std::string> color(std::string_view container_uuid,
                                   std::int64_t scene_item_id) const;
  json diagnostics() const;

private:
  static std::string key(std::string_view container_uuid, std::int64_t scene_item_id);

  ISceneItemPropertiesSource* source_ = nullptr;
  mutable std::mutex mutex_;
  bool observed_ = false;
  std::uint64_t source_sequence_ = 0;
  std::unordered_map<std::string, std::string> colors_;
};

} // namespace studio_remote
