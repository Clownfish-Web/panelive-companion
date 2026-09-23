#include "obs_scene_item_properties.hpp"

#ifdef __APPLE__
#include "../../platform/macos/ipc/macos_ipc_names.hpp"
#endif

#include <util/platform.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace studio_remote::obs_plugin
{
namespace
{
constexpr std::uint64_t refresh_interval_ns = 500'000'000ULL;
constexpr std::array<std::string_view, 8> preset_colors{
    "#55FF4444",
    "#55FFFF44",
    "#5544FF44",
    "#5544FFFF",
    "#554444FF",
    "#55FF44FF",
    "#55444444",
    "#55FFFFFF",
};

#if !defined(_WIN32) && !defined(__APPLE__)
template <typename Mapping> Mapping* create_mapping(const char* name, int& descriptor)
{
  const auto candidate = shm_open(name, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (candidate < 0)
    return nullptr;
  struct stat status{};
  if (fstat(candidate, &status) != 0 || status.st_uid != geteuid() ||
      fchmod(candidate, 0600) != 0 ||
      ftruncate(candidate, static_cast<off_t>(sizeof(Mapping))) != 0)
  {
    close(candidate);
    return nullptr;
  }
  auto* address = mmap(nullptr, sizeof(Mapping), PROT_READ | PROT_WRITE, MAP_SHARED, candidate, 0);
  if (address == MAP_FAILED)
  {
    close(candidate);
    return nullptr;
  }
  descriptor = candidate;
  return static_cast<Mapping*>(address);
}
#endif

void copy_text(char* target, std::size_t capacity, std::string_view value)
{
  const auto size = std::min(capacity - 1, value.size());
  std::memcpy(target, value.data(), size);
  target[size] = '\0';
}

bool valid_custom_color(std::string_view value)
{
  return value.size() == 9 && value.front() == '#' &&
         std::all_of(value.begin() + 1,
                     value.end(),
                     [](unsigned char character) { return std::isxdigit(character) != 0; });
}

std::string item_color(obs_sceneitem_t* item)
{
  obs_data_t* settings = obs_sceneitem_get_private_settings(item);
  if (!settings)
    return {};
  const auto preset = obs_data_get_int(settings, "color-preset");
  std::string color;
  if (preset == 1)
  {
    const auto* custom = obs_data_get_string(settings, "color");
    if (custom && valid_custom_color(custom))
    {
      color = custom;
      std::transform(color.begin() + 1,
                     color.end(),
                     color.begin() + 1,
                     [](unsigned char character)
                     { return static_cast<char>(std::toupper(character)); });
    }
  }
  else if (preset >= 2 && preset <= 9)
  {
    color = preset_colors[static_cast<std::size_t>(preset - 2)];
  }
  obs_data_release(settings);
  return color;
}

struct EnumerationContext
{
  std::string container_uuid;
  std::vector<ipc::SceneItemPropertiesEntry>* entries = nullptr;
  bool failed = false;
};

bool enumerate_items(obs_scene_t*, obs_sceneitem_t* item, void* data) noexcept
{
  auto& context = *static_cast<EnumerationContext*>(data);
  try
  {
    const auto color = item_color(item);
    if (!color.empty())
    {
      ipc::SceneItemPropertiesEntry entry;
      copy_text(entry.container_uuid, sizeof(entry.container_uuid), context.container_uuid);
      entry.scene_item_id = obs_sceneitem_get_id(item);
      copy_text(entry.color, sizeof(entry.color), color);
      context.entries->push_back(entry);
    }

    if (obs_sceneitem_is_group(item))
    {
      obs_source_t* group_source = obs_sceneitem_get_source(item);
      const char* group_uuid = group_source ? obs_source_get_uuid(group_source) : nullptr;
      if (group_uuid && *group_uuid)
      {
        EnumerationContext children{group_uuid, context.entries};
        obs_sceneitem_group_enum_items(item, enumerate_items, &children);
        context.failed = context.failed || children.failed;
      }
    }
  }
  catch (...)
  {
    context.failed = true;
    return false;
  }
  return !context.failed;
}

bool same_entries(const std::vector<ipc::SceneItemPropertiesEntry>& left,
                  const std::vector<ipc::SceneItemPropertiesEntry>& right)
{
  return left.size() == right.size() &&
         std::equal(left.begin(),
                    left.end(),
                    right.begin(),
                    [](const auto& first, const auto& second)
                    { return std::memcmp(&first, &second, sizeof(first)) == 0; });
}
} // namespace

ObsSceneItemProperties::~ObsSceneItemProperties()
{
  stop();
}

bool ObsSceneItemProperties::start()
{
  if (started_)
    return true;
  if (!open_mapping())
    return false;
  obs_add_main_render_callback(on_main_render, this);
  started_ = true;
  return true;
}

void ObsSceneItemProperties::stop()
{
  if (started_)
    obs_remove_main_render_callback(on_main_render, this);
  started_ = false;
  close_mapping();
}

void ObsSceneItemProperties::on_main_render(void* context, std::uint32_t, std::uint32_t)
{
  try
  {
    if (context)
      static_cast<ObsSceneItemProperties*>(context)->refresh();
  }
  catch (...)
  {
    blog(LOG_ERROR, "[PaneLive Companion] Scene item property publication failed unexpectedly");
  }
}

void ObsSceneItemProperties::refresh()
{
  const auto now = os_gettime_ns();
  if (now - last_refresh_ns_ < refresh_interval_ns)
    return;
  last_refresh_ns_ = now;

  std::vector<ipc::SceneItemPropertiesEntry> entries;
  bool failed = false;
  std::pair enumeration_result{&entries, &failed};
  obs_enum_scenes(
      [](void* data, obs_source_t* source) noexcept
      {
        auto& result =
            *static_cast<std::pair<std::vector<ipc::SceneItemPropertiesEntry>*, bool*>*>(data);
        try
        {
          const char* uuid = obs_source_get_uuid(source);
          obs_scene_t* scene = obs_scene_from_source(source);
          if (!uuid || !*uuid || !scene)
            return true;
          EnumerationContext context{uuid, result.first};
          obs_scene_enum_items(scene, enumerate_items, &context);
          *result.second = *result.second || context.failed;
          return !context.failed;
        }
        catch (...)
        {
          *result.second = true;
          return false;
        }
      },
      &enumeration_result);
  if (failed)
    return;

  std::sort(entries.begin(),
            entries.end(),
            [](const auto& left, const auto& right)
            {
              const auto container = std::strcmp(left.container_uuid, right.container_uuid);
              return container != 0 ? container < 0 : left.scene_item_id < right.scene_item_id;
            });
  entries.erase(std::unique(entries.begin(),
                            entries.end(),
                            [](const auto& left, const auto& right)
                            {
                              return std::strcmp(left.container_uuid, right.container_uuid) == 0 &&
                                     left.scene_item_id == right.scene_item_id;
                            }),
                entries.end());
  if (entries.size() > ipc::maximum_scene_item_property_entries)
    entries.resize(ipc::maximum_scene_item_property_entries);
  if (!same_entries(entries, published_entries_))
  {
    publish(entries);
    published_entries_ = std::move(entries);
  }
  else if (sequence_ == 0)
  {
    publish(entries);
  }
}

bool ObsSceneItemProperties::open_mapping()
{
#ifdef _WIN32
  if (mapping_)
    return true;
  auto memory = windows::SharedMemoryOwner::create(ipc::scene_item_properties_mapping_name,
                                                   sizeof(ipc::SceneItemPropertiesMapping));
  if (!memory)
    return false;
  mapping_ = reinterpret_cast<ipc::SceneItemPropertiesMapping*>(memory->bytes().data());
  mapping_owner_ = std::move(memory);
#elif defined(__APPLE__)
  mapping_owner_ =
      macos::SharedMemoryOwner::create(studio_remote::macos::ipc_names::scene_item_properties,
                                       sizeof(ipc::SceneItemPropertiesMapping));
  if (!mapping_owner_)
    return false;
  mapping_ = reinterpret_cast<ipc::SceneItemPropertiesMapping*>(mapping_owner_->bytes().data());
#else
  mapping_ = create_mapping<ipc::SceneItemPropertiesMapping>(
      ipc::scene_item_properties_shared_memory_name, mapping_handle_);
  if (!mapping_)
    return false;
#endif
  std::construct_at(mapping_);
  mapping_->structure_size = sizeof(ipc::SceneItemPropertiesMapping);
#ifdef _WIN32
  mapping_->producer_process_id = GetCurrentProcessId();
#else
  mapping_->producer_process_id = static_cast<std::uint32_t>(getpid());
#endif
  return true;
}

void ObsSceneItemProperties::close_mapping()
{
#ifdef _WIN32
  mapping_ = nullptr;
  mapping_owner_.reset();
#elif defined(__APPLE__)
  mapping_ = nullptr;
  mapping_owner_.reset();
#else
  if (mapping_)
    munmap(mapping_, sizeof(ipc::SceneItemPropertiesMapping));
  if (mapping_handle_ >= 0)
    close(mapping_handle_);
  mapping_ = nullptr;
  mapping_handle_ = -1;
#endif
}

void ObsSceneItemProperties::publish(const std::vector<ipc::SceneItemPropertiesEntry>& entries)
{
  if (!mapping_)
    return;
  ++sequence_;
  mapping_->sequence.store(sequence_, std::memory_order_release);
  const auto count = std::min(entries.size(), ipc::maximum_scene_item_property_entries);
  mapping_->entry_count = static_cast<std::uint32_t>(count);
  std::copy_n(entries.begin(), count, mapping_->entries);
  ++sequence_;
  mapping_->sequence.store(sequence_, std::memory_order_release);
}

} // namespace studio_remote::obs_plugin
