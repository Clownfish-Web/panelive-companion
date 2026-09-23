#include "obs_audio_mixer_roster.hpp"
#include "obs_audio_monitor.hpp"
#include "obs_companion_status_ui.hpp"
#include "obs_frame_capture.hpp"
#include "obs_quick_transition_control.hpp"
#include "obs_runtime_capabilities_publisher.hpp"
#include "obs_scene_item_properties.hpp"
#include "obs_source_thumbnail_renderer.hpp"
#include "shared_frame_publisher.hpp"
#include "shared_thumbnail_mailbox.hpp"

#include <obs-module.h>

#include <exception>
#include <memory>

namespace
{

// OBS accepts plugins that report an older API version as long as they use
// only that version's stable ABI. Every OBS function used by this module was
// already public in OBS 28. Reporting that floor keeps one binary loadable by
// OBS 28 and later instead of tying it to the build machine's OBS 32 headers.
constexpr std::uint32_t minimum_obs_api_version = MAKE_SEMANTIC_VERSION(28, 0, 0);

obs_module_t* module_pointer = nullptr;

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

namespace
{

std::unique_ptr<studio_remote::obs_plugin::SharedFramePublisher> publisher;
std::unique_ptr<studio_remote::obs_plugin::ObsFrameCapture> capture;
std::unique_ptr<studio_remote::obs_plugin::ObsCompanionStatusUi> companion_status_ui;
std::unique_ptr<studio_remote::obs_plugin::SharedThumbnailMailbox> thumbnail_mailbox;
std::unique_ptr<studio_remote::obs_plugin::ObsSourceThumbnailRenderer> thumbnail_renderer;
std::unique_ptr<studio_remote::obs_plugin::ObsAudioMixerRoster> audio_mixer_roster;
std::unique_ptr<studio_remote::obs_plugin::ObsAudioMonitor> audio_monitor;
std::unique_ptr<studio_remote::obs_plugin::ObsSceneItemProperties> scene_item_properties;
std::unique_ptr<studio_remote::obs_plugin::ObsQuickTransitionControl> quick_transition_control;
std::unique_ptr<studio_remote::obs_plugin::ObsRuntimeCapabilitiesPublisher> runtime_capabilities;

} // namespace

MODULE_EXPORT const char* obs_module_name()
{
  return "PaneLive Companion";
}

MODULE_EXPORT const char* obs_module_description()
{
  return "Headless Program and Preview frame bridge for PaneLive Companion";
}

namespace
{
bool start_capture_components()
{
  runtime_capabilities =
      std::make_unique<studio_remote::obs_plugin::ObsRuntimeCapabilitiesPublisher>();
  if (!runtime_capabilities->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create OBS runtime capability IPC");
    return false;
  }

  publisher = std::make_unique<studio_remote::obs_plugin::SharedFramePublisher>();
  if (!publisher->open())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create video frame IPC");
    return false;
  }

  capture = std::make_unique<studio_remote::obs_plugin::ObsFrameCapture>(*publisher);
  if (!capture->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not start OBS frame capture");
    return false;
  }
  companion_status_ui = std::make_unique<studio_remote::obs_plugin::ObsCompanionStatusUi>();
  if (!companion_status_ui->start(*capture))
    blog(LOG_WARNING, "[PaneLive Companion] Could not initialize Companion status UI");
  capture->on_status(
      [](studio_remote::obs_plugin::CompanionStatusSnapshot snapshot)
      {
        if (companion_status_ui)
          companion_status_ui->post_status(std::move(snapshot));
      });

  return true;
}
bool start_thumbnail_components()
{
  thumbnail_mailbox = std::make_unique<studio_remote::obs_plugin::SharedThumbnailMailbox>();
  if (!thumbnail_mailbox->open())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create source thumbnail IPC");
    return false;
  }
  thumbnail_renderer =
      std::make_unique<studio_remote::obs_plugin::ObsSourceThumbnailRenderer>(*thumbnail_mailbox);
  if (!thumbnail_renderer->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not start source thumbnail renderer");
    return false;
  }

  return true;
}
bool start_control_components()
{
  audio_mixer_roster = std::make_unique<studio_remote::obs_plugin::ObsAudioMixerRoster>();
  if (!audio_mixer_roster->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create audio mixer roster IPC");
    return false;
  }

  audio_monitor = std::make_unique<studio_remote::obs_plugin::ObsAudioMonitor>();
  if (!audio_monitor->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create audio monitor IPC");
    audio_monitor.reset();
  }

  scene_item_properties = std::make_unique<studio_remote::obs_plugin::ObsSceneItemProperties>();
  if (!scene_item_properties->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create scene item properties IPC");
    return false;
  }

  quick_transition_control =
      std::make_unique<studio_remote::obs_plugin::ObsQuickTransitionControl>();
  if (!quick_transition_control->start())
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not create quick transition control IPC");
    return false;
  }

  return true;
}
} // namespace

bool obs_module_load()
{
  try
  {
    if (start_capture_components() && start_thumbnail_components() && start_control_components())
    {
      blog(LOG_INFO, "[PaneLive Companion] Headless video frame bridge started");
      return true;
    }
  }
  catch (const std::exception&)
  {
    blog(LOG_ERROR, "[PaneLive Companion] Helper startup failed with an exception");
  }
  catch (...)
  {
    blog(LOG_ERROR, "[PaneLive Companion] Unexpected helper startup failure");
  }
  obs_module_unload();
  return false;
}

void obs_module_unload()
{
  quick_transition_control.reset();
  scene_item_properties.reset();
  audio_monitor.reset();
  audio_mixer_roster.reset();
  thumbnail_renderer.reset();
  thumbnail_mailbox.reset();
  if (capture)
    capture->on_status({});
  companion_status_ui.reset();
  capture.reset();
  publisher.reset();
  runtime_capabilities.reset();
}
