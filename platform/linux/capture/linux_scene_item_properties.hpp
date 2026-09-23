#pragma once

#include <studio_remote/ipc/scene_item_properties_mapping.hpp>
#include <studio_remote/scene/scene_item_properties_source.hpp>

#include <string>

namespace studio_remote::linux
{

class LinuxSceneItemProperties final : public ISceneItemPropertiesSource
{
public:
  explicit LinuxSceneItemProperties(
      std::string mapping_name = ipc::scene_item_properties_shared_memory_name);
  SceneItemPropertiesObservation read() override;

private:
  std::string mapping_name_;
};

} // namespace studio_remote::linux
