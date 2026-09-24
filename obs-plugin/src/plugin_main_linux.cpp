#include "obs_audio_mixer_roster.hpp"
#include "obs_audio_monitor.hpp"
#include "obs_frame_capture.hpp"
#include "obs_quick_transition_control.hpp"
#include "obs_runtime_capabilities_publisher.hpp"
#include "obs_scene_item_properties.hpp"
#include "obs_source_thumbnail_renderer.hpp"
#include "shared_frame_publisher.hpp"
#include "shared_thumbnail_mailbox.hpp"

#include <chrono>
#include <memory>
#include <obs-module.h>

namespace
{
constexpr std::uint32_t minimum_obs_api_version = MAKE_SEMANTIC_VERSION(30, 0, 0);
obs_module_t* module_pointer = nullptr;
std::unique_ptr<studio_remote::obs_plugin::SharedFramePublisher> publisher;
std::unique_ptr<studio_remote::obs_plugin::ObsFrameCapture> capture;
std::unique_ptr<studio_remote::obs_plugin::ObsAudioMonitor> audio_monitor;
std::unique_ptr<studio_remote::obs_plugin::ObsAudioMixerRoster> audio_mixer_roster;
std::unique_ptr<studio_remote::obs_plugin::ObsRuntimeCapabilitiesPublisher> runtime_capabilities;
std::unique_ptr<studio_remote::obs_plugin::ObsSceneItemProperties> scene_item_properties;
std::unique_ptr<studio_remote::obs_plugin::ObsQuickTransitionControl> quick_transition_control;
std::unique_ptr<studio_remote::obs_plugin::SharedThumbnailMailbox> thumbnail_mailbox;
std::unique_ptr<studio_remote::obs_plugin::ObsSourceThumbnailRenderer> thumbnail_renderer;

void poll_capture_demand(void*, float)
{
  if (!capture)
    return;
  const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
  capture->poll_demand(static_cast<std::uint64_t>(now));
}
} // namespace

OBS_MODULE_USE_DEFAULT_LOCALE("panelive-companion-obs-plugin", "en-US")

MODULE_EXPORT void obs_module_set_pointer(obs_module_t* module)
{
  module_pointer = module;
}

obs_module_t* obs_current_module()
{
  return module_pointer;
}

MODULE_EXPORT std::uint32_t obs_module_ver()
{
  return minimum_obs_api_version;
}

MODULE_EXPORT const char* obs_module_name()
{
  return "PaneLive Companion";
}

MODULE_EXPORT const char* obs_module_description()
{
  return "Headless Program and Preview frame bridge for PaneLive Companion";
}

bool obs_module_load()
{
  runtime_capabilities =
      std::make_unique<studio_remote::obs_plugin::ObsRuntimeCapabilitiesPublisher>();
  if (!runtime_capabilities->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create POSIX runtime capability IPC");
    runtime_capabilities.reset();
    return false;
  }
  publisher = std::make_unique<studio_remote::obs_plugin::SharedFramePublisher>();
  if (!publisher->open())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create POSIX video frame IPC");
    publisher.reset();
    runtime_capabilities.reset();
    return false;
  }
  capture = std::make_unique<studio_remote::obs_plugin::ObsFrameCapture>(*publisher);
  if (!capture->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not start OBS frame capture");
    capture.reset();
    publisher.reset();
    runtime_capabilities.reset();
    return false;
  }
  audio_monitor = std::make_unique<studio_remote::obs_plugin::ObsAudioMonitor>();
  if (!audio_monitor->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create POSIX audio monitor IPC");
    audio_monitor.reset();
  }
  audio_mixer_roster = std::make_unique<studio_remote::obs_plugin::ObsAudioMixerRoster>();
  if (!audio_mixer_roster->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create POSIX audio mixer roster IPC");
    audio_mixer_roster.reset();
    audio_monitor.reset();
    capture.reset();
    publisher.reset();
    runtime_capabilities.reset();
    return false;
  }
  scene_item_properties = std::make_unique<studio_remote::obs_plugin::ObsSceneItemProperties>();
  if (!scene_item_properties->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create POSIX scene item property IPC");
    scene_item_properties.reset();
    audio_mixer_roster.reset();
    audio_monitor.reset();
    capture.reset();
    publisher.reset();
    runtime_capabilities.reset();
    return false;
  }
  quick_transition_control =
      std::make_unique<studio_remote::obs_plugin::ObsQuickTransitionControl>();
  if (!quick_transition_control->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create POSIX quick transition IPC");
    quick_transition_control.reset();
    scene_item_properties.reset();
    audio_mixer_roster.reset();
    audio_monitor.reset();
    capture.reset();
    publisher.reset();
    runtime_capabilities.reset();
    return false;
  }
  thumbnail_mailbox = std::make_unique<studio_remote::obs_plugin::SharedThumbnailMailbox>();
  if (!thumbnail_mailbox->open())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create POSIX source thumbnail IPC");
    thumbnail_mailbox.reset();
    quick_transition_control.reset();
    scene_item_properties.reset();
    audio_mixer_roster.reset();
    audio_monitor.reset();
    capture.reset();
    publisher.reset();
    runtime_capabilities.reset();
    return false;
  }
  thumbnail_renderer =
      std::make_unique<studio_remote::obs_plugin::ObsSourceThumbnailRenderer>(*thumbnail_mailbox);
  if (!thumbnail_renderer->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not start source thumbnail renderer");
    thumbnail_renderer.reset();
    thumbnail_mailbox.reset();
    quick_transition_control.reset();
    scene_item_properties.reset();
    audio_mixer_roster.reset();
    audio_monitor.reset();
    capture.reset();
    publisher.reset();
    runtime_capabilities.reset();
    return false;
  }
  obs_add_tick_callback(poll_capture_demand, nullptr);
  blog(LOG_INFO, "[PaneLive Companion] Linux headless media bridge started");
  return true;
}

void obs_module_unload()
{
  obs_remove_tick_callback(poll_capture_demand, nullptr);
  thumbnail_renderer.reset();
  thumbnail_mailbox.reset();
  quick_transition_control.reset();
  scene_item_properties.reset();
  audio_mixer_roster.reset();
  audio_monitor.reset();
  capture.reset();
  publisher.reset();
  runtime_capabilities.reset();
}
