#pragma once

#include "../ipc/macos_ipc_names.hpp"
#include <string>

#include <studio_remote/scene/scene_item_properties_source.hpp>

namespace studio_remote::macos
{

class MacosSceneItemProperties final : public ISceneItemPropertiesSource
{
public:
  explicit MacosSceneItemProperties(std::string mapping_name = ipc_names::scene_item_properties);
  SceneItemPropertiesObservation read() override;

private:
  const std::string mapping_name_;
};

} // namespace studio_remote::macos
