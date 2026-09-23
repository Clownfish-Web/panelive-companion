#pragma once

#ifdef _WIN32
#include "windows/shared_memory_owner.hpp"
#elif defined(__APPLE__)
#include "macos/shared_memory_owner.hpp"
#endif

#include <chrono>
#include <cstdint>
#include <studio_remote/ipc/quick_transition_control_mapping.hpp>
#include <unordered_map>

namespace studio_remote::obs_plugin
{

class ObsQuickTransitionControl
{
public:
  ObsQuickTransitionControl() = default;
  ~ObsQuickTransitionControl();

  ObsQuickTransitionControl(const ObsQuickTransitionControl&) = delete;
  ObsQuickTransitionControl& operator=(const ObsQuickTransitionControl&) = delete;

  bool start();
  void stop();

private:
  static void on_main_render(void* context, std::uint32_t width, std::uint32_t height);
  void process_command();
  void publish_catalog();
  bool open_mapping();
  void close_mapping();

#ifdef _WIN32
  std::unique_ptr<windows::SharedMemoryOwner> mapping_owner_;
#elif defined(__APPLE__)
  std::unique_ptr<macos::SharedMemoryOwner> mapping_owner_;
#else
  int mapping_handle_ = -1;
#endif
  ipc::QuickTransitionControlMapping* mapping_ = nullptr;
  std::uint64_t processed_sequence_ = 0;
  std::uint64_t catalog_sequence_ = 0;
  std::uint64_t hotkey_catalog_sequence_ = 0;
  std::unordered_map<std::int32_t, std::uint32_t> configured_durations_;
  std::chrono::steady_clock::time_point next_catalog_publish_{};
  bool callback_registered_ = false;
};

} // namespace studio_remote::obs_plugin
