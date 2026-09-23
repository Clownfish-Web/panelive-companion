#pragma once

#include <studio_remote/scene/scene_item_properties_source.hpp>

namespace studio_remote::windows
{

class WindowsSceneItemProperties final : public ISceneItemPropertiesSource
{
public:
  SceneItemPropertiesObservation read() override;
};

} // namespace studio_remote::windows
