#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote
{

struct SceneItemPropertiesValue
{
  std::string container_uuid;
  std::int64_t scene_item_id = 0;
  std::string color;
  bool operator==(const SceneItemPropertiesValue&) const = default;
};

struct SceneItemPropertiesObservation
{
  bool available = false;
  std::uint64_t sequence = 0;
  std::vector<SceneItemPropertiesValue> values;
};

class ISceneItemPropertiesSource
{
public:
  virtual ~ISceneItemPropertiesSource() = default;
  virtual SceneItemPropertiesObservation read() = 0;
};

} // namespace studio_remote
