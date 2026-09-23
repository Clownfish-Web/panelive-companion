#pragma once

#include "recording_pause_policy.hpp"

#ifdef _WIN32
#include "windows/shared_memory_owner.hpp"
#elif defined(__APPLE__)
#include "macos/shared_memory_owner.hpp"
#endif
#include <obs-frontend-api.h>
#include <studio_remote/ipc/obs_runtime_capabilities_mapping.hpp>

namespace studio_remote::obs_plugin
{

class ObsRuntimeCapabilitiesPublisher
{
public:
  ObsRuntimeCapabilitiesPublisher();
  ~ObsRuntimeCapabilitiesPublisher();

  ObsRuntimeCapabilitiesPublisher(const ObsRuntimeCapabilitiesPublisher&) = delete;
  ObsRuntimeCapabilitiesPublisher& operator=(const ObsRuntimeCapabilitiesPublisher&) = delete;

  bool start();
  void stop();

private:
  static void handle_frontend_event(obs_frontend_event event, void* context);
  static void on_tick(void* context, float seconds);
  void frontend_finished_loading();
  void frontend_exiting();
  void publish();
  void publish(bool recording_pause_available);

#ifdef _WIN32
  std::unique_ptr<windows::SharedMemoryOwner> mapping_owner_;
#elif defined(__APPLE__)
  std::unique_ptr<macos::SharedMemoryOwner> mapping_owner_;
#else
  int mapping_handle_ = -1;
#endif
  ipc::ObsRuntimeCapabilitiesMapping* mapping_ = nullptr;
  RecordingPauseProbeLifecycle probe_lifecycle_;
  bool callback_registered_ = false;
  bool tick_registered_ = false;
  std::uint64_t last_publication_ns_ = 0;
};

} // namespace studio_remote::obs_plugin
