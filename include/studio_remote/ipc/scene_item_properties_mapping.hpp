#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace studio_remote::ipc
{

inline constexpr wchar_t scene_item_properties_mapping_name[] =
    L"Local\\panelive-companion.scene-item-properties.v1";
inline constexpr char scene_item_properties_shared_memory_name[] =
    "/clownfishweb-panelive-scene-item-properties-v1";
inline constexpr std::uint32_t scene_item_properties_magic = 0x53495052;
inline constexpr std::uint32_t scene_item_properties_version = 1;
inline constexpr std::size_t maximum_scene_item_property_entries = 1'024;

struct SceneItemPropertiesEntry
{
  char container_uuid[129]{};
  std::int64_t scene_item_id = 0;
  char color[10]{};
};

struct SceneItemPropertiesMapping
{
  std::uint32_t magic = scene_item_properties_magic;
  std::uint32_t version = scene_item_properties_version;
  std::uint32_t structure_size = 0;
  std::uint32_t producer_process_id = 0;
  std::uint32_t entry_count = 0;
  std::uint32_t reserved = 0;
  SceneItemPropertiesEntry entries[maximum_scene_item_property_entries]{};
  alignas(8) std::atomic<std::uint64_t> sequence{0};
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

} // namespace studio_remote::ipc
