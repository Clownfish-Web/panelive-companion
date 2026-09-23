#pragma once

#include <studio_remote/ipc/scene_item_properties_mapping.hpp>

#ifdef _WIN32
#include "windows/shared_memory_owner.hpp"
#elif defined(__APPLE__)
#include "macos/shared_memory_owner.hpp"
#endif
#include <obs.h>

#include <cstdint>
#include <vector>

namespace studio_remote::obs_plugin
{

class ObsSceneItemProperties
{
public:
  ObsSceneItemProperties() = default;
  ~ObsSceneItemProperties();

  ObsSceneItemProperties(const ObsSceneItemProperties&) = delete;
  ObsSceneItemProperties& operator=(const ObsSceneItemProperties&) = delete;

  bool start();
  void stop();

private:
  static void on_main_render(void* context, std::uint32_t width, std::uint32_t height);
  void refresh();
  bool open_mapping();
  void close_mapping();
  void publish(const std::vector<ipc::SceneItemPropertiesEntry>& entries);

#ifdef _WIN32
  std::unique_ptr<windows::SharedMemoryOwner> mapping_owner_;
#elif defined(__APPLE__)
  std::unique_ptr<macos::SharedMemoryOwner> mapping_owner_;
#else
  int mapping_handle_ = -1;
#endif
  ipc::SceneItemPropertiesMapping* mapping_ = nullptr;
  std::vector<ipc::SceneItemPropertiesEntry> published_entries_;
  std::uint64_t last_refresh_ns_ = 0;
  std::uint64_t sequence_ = 0;
  bool started_ = false;
};

} // namespace studio_remote::obs_plugin
