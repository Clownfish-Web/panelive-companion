#include "windows_scene_item_properties.hpp"

#include <studio_remote/ipc/scene_item_properties_mapping.hpp>

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace studio_remote::windows
{
namespace
{
std::string bounded_string(const char* value, std::size_t capacity)
{
  return std::string(value, strnlen_s(value, capacity));
}

bool valid_color(std::string_view value)
{
  return value.size() == 9 && value.front() == '#' &&
         std::all_of(value.begin() + 1,
                     value.end(),
                     [](unsigned char character) { return std::isxdigit(character) != 0; });
}
} // namespace

SceneItemPropertiesObservation WindowsSceneItemProperties::read()
{
  HANDLE handle = OpenFileMappingW(FILE_MAP_READ, FALSE, ipc::scene_item_properties_mapping_name);
  if (!handle)
    return {};
  auto* mapping = static_cast<ipc::SceneItemPropertiesMapping*>(
      MapViewOfFile(handle, FILE_MAP_READ, 0, 0, sizeof(ipc::SceneItemPropertiesMapping)));
  if (!mapping)
  {
    CloseHandle(handle);
    return {};
  }

  SceneItemPropertiesObservation observation;
  if (mapping->magic == ipc::scene_item_properties_magic &&
      mapping->version == ipc::scene_item_properties_version &&
      mapping->structure_size == sizeof(ipc::SceneItemPropertiesMapping))
  {
    for (int attempt = 0; attempt < 3; ++attempt)
    {
      const auto before = mapping->sequence.load(std::memory_order_acquire);
      if ((before & 1U) != 0)
        continue;
      const auto count =
          std::min<std::size_t>(mapping->entry_count, ipc::maximum_scene_item_property_entries);
      std::vector<SceneItemPropertiesValue> values;
      values.reserve(count);
      for (std::size_t index = 0; index < count; ++index)
      {
        const auto& entry = mapping->entries[index];
        auto color = bounded_string(entry.color, sizeof(entry.color));
        if (!valid_color(color))
          continue;
        values.push_back({bounded_string(entry.container_uuid, sizeof(entry.container_uuid)),
                          entry.scene_item_id,
                          std::move(color)});
      }
      const auto after = mapping->sequence.load(std::memory_order_acquire);
      if (before == after && (after & 1U) == 0)
      {
        observation = {after != 0, after, std::move(values)};
        break;
      }
    }
  }
  UnmapViewOfFile(mapping);
  CloseHandle(handle);
  return observation;
}

} // namespace studio_remote::windows
