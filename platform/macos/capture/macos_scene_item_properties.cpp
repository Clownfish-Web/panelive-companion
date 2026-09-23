#include "macos_scene_item_properties.hpp"

#include <studio_remote/ipc/scene_item_properties_mapping.hpp>

#include "../ipc/read_only_shared_memory.hpp"
#include <utility>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace studio_remote::macos
{
namespace
{
std::string bounded_string(const char* value, std::size_t capacity)
{
  return std::string(value, strnlen(value, capacity));
}

bool valid_color(std::string_view value)
{
  return value.size() == 9 && value.front() == '#' &&
         std::all_of(value.begin() + 1,
                     value.end(),
                     [](unsigned char character) { return std::isxdigit(character) != 0; });
}
} // namespace

MacosSceneItemProperties::MacosSceneItemProperties(std::string mapping_name)
    : mapping_name_(std::move(mapping_name))
{
}

SceneItemPropertiesObservation MacosSceneItemProperties::read()
{
  auto region = ReadOnlySharedMemory::open(mapping_name_, sizeof(ipc::SceneItemPropertiesMapping));
  if (!region)
    return {};
  const auto* mapping =
      reinterpret_cast<const ipc::SceneItemPropertiesMapping*>(region->bytes().data());

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
  return observation;
}

} // namespace studio_remote::macos
