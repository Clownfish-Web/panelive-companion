#include "linux_scene_item_properties.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace studio_remote::linux
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

class ReadOnlySceneItemMapping
{
public:
  explicit ReadOnlySceneItemMapping(std::string_view name)
  {
    descriptor_ = shm_open(std::string(name).c_str(), O_RDONLY | O_CLOEXEC, 0);
    struct stat status
    {
    };
    if (descriptor_ < 0 || fstat(descriptor_, &status) != 0 || status.st_uid != geteuid() ||
        (status.st_mode & 0077) != 0 ||
        status.st_size < static_cast<off_t>(sizeof(ipc::SceneItemPropertiesMapping)))
    {
      return;
    }
    auto* address = mmap(
        nullptr, sizeof(ipc::SceneItemPropertiesMapping), PROT_READ, MAP_SHARED, descriptor_, 0);
    if (address != MAP_FAILED)
      mapping_ = static_cast<const ipc::SceneItemPropertiesMapping*>(address);
  }

  ~ReadOnlySceneItemMapping()
  {
    if (mapping_)
      munmap(const_cast<ipc::SceneItemPropertiesMapping*>(mapping_),
             sizeof(ipc::SceneItemPropertiesMapping));
    if (descriptor_ >= 0)
      close(descriptor_);
  }

  ReadOnlySceneItemMapping(const ReadOnlySceneItemMapping&) = delete;
  ReadOnlySceneItemMapping& operator=(const ReadOnlySceneItemMapping&) = delete;

  const ipc::SceneItemPropertiesMapping* get() const noexcept
  {
    return mapping_;
  }

private:
  int descriptor_ = -1;
  const ipc::SceneItemPropertiesMapping* mapping_ = nullptr;
};
} // namespace

LinuxSceneItemProperties::LinuxSceneItemProperties(std::string mapping_name)
    : mapping_name_(std::move(mapping_name))
{
}

SceneItemPropertiesObservation LinuxSceneItemProperties::read()
{
  ReadOnlySceneItemMapping owner(mapping_name_);
  const auto* mapping = owner.get();
  if (!mapping || mapping->magic != ipc::scene_item_properties_magic ||
      mapping->version != ipc::scene_item_properties_version ||
      mapping->structure_size != sizeof(ipc::SceneItemPropertiesMapping))
  {
    return {};
  }

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
      return {after != 0, after, std::move(values)};
  }
  return {};
}

} // namespace studio_remote::linux
