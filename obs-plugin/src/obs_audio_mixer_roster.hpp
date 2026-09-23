#pragma once

#include <studio_remote/ipc/audio_mixer_roster_mapping.hpp>

#ifdef _WIN32
#include "windows/shared_memory_owner.hpp"
#elif defined(__APPLE__)
#include "macos/shared_memory_owner.hpp"
#endif
#include <obs-frontend-api.h>
#include <obs.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace studio_remote::obs_plugin
{

class ObsAudioMixerRoster
{
public:
  ObsAudioMixerRoster() = default;
  ~ObsAudioMixerRoster();

  ObsAudioMixerRoster(const ObsAudioMixerRoster&) = delete;
  ObsAudioMixerRoster& operator=(const ObsAudioMixerRoster&) = delete;

  bool start();
  void stop();

private:
  static void on_main_render(void* context, std::uint32_t width, std::uint32_t height);
  static void handle_frontend_event(obs_frontend_event event, void* context);
  void refresh();
  bool open_mapping();
  void close_mapping();
  void publish(const std::vector<ipc::AudioMixerRosterEntry>& entries);

#ifdef _WIN32
  std::unique_ptr<windows::SharedMemoryOwner> mapping_owner_;
#elif defined(__APPLE__)
  std::unique_ptr<macos::SharedMemoryOwner> mapping_owner_;
#else
  int mapping_handle_ = -1;
#endif
  ipc::AudioMixerRosterMapping* mapping_ = nullptr;
  std::unordered_set<std::string> known_control_uuids_;
  std::uint64_t last_refresh_ns_ = 0;
  std::uint64_t sequence_ = 0;
  bool started_ = false;
  bool frontend_callback_registered_ = false;
  // Set on OBS_FRONTEND_EVENT_EXIT: render callbacks may still fire after OBS destroyed its
  // frontend API, and the roster queries that API on every refresh.
  bool frontend_exited_ = false;
};

} // namespace studio_remote::obs_plugin
