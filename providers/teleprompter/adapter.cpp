#include <studio_remote/teleprompter/adapter.hpp>

#include <stdexcept>
#include <utility>

namespace studio_remote::teleprompter
{
void Adapter::start()
{
  set_running(true);
}

void Adapter::stop()
{
  set_running(false);
}

void Adapter::set_running(bool running)
{
  ConnectionCallback callback;
  {
    std::lock_guard lock(mutex_);
    if (running_ == running)
      return;
    running_ = running;
    callback = connection_callback_;
  }
  if (callback)
    callback(running, {});
}

bool Adapter::connected() const
{
  std::lock_guard lock(mutex_);
  return running_;
}

json Adapter::request(std::string_view, json, std::source_location, std::string_view)
{
  throw std::logic_error("PaneLive Teleprompter does not support production requests");
}

void Adapter::on_event(EventCallback)
{
  // This local provider never emits production events.
}

void Adapter::on_connection(ConnectionCallback callback)
{
  {
    std::lock_guard lock(mutex_);
    connection_callback_.swap(callback);
  }
}

ProductionBackendInfo Adapter::backend_info() const
{
  ProductionBackendInfo info;
  info.id = "companion-teleprompter";
  info.display_name = "PaneLive Teleprompter";
  info.kind = ProductionBackendKind::companion_teleprompter;
  auto& capabilities = info.capabilities;
  capabilities.preview = false;
  capabilities.program_observation = false;
  capabilities.system_hotkeys = false;
  capabilities.program_selection = false;
  capabilities.preview_selection = false;
  capabilities.composition_monitor_render = false;
  capabilities.native_audio_roster_required = false;
  capabilities.layer_visibility = false;
  capabilities.layer_colors = false;
  capabilities.transition_catalog = false;
  capabilities.transition_configure = false;
  capabilities.transition_execute = false;
  capabilities.program_video = false;
  capabilities.preview_video = false;
  capabilities.thumbnails = false;
  capabilities.audio_meters = false;
  capabilities.audio_monitoring = false;
  capabilities.audio_mute = false;
  capabilities.audio_volume = false;
  capabilities.audio_source_audition_bus = false;
  capabilities.recording_pause = false;
  capabilities.recording_observe = false;
  capabilities.recording_start_stop = false;
  capabilities.streaming_observe = false;
  capabilities.streaming_start_stop = false;
  capabilities.streaming_destinations = false;
  capabilities.stream_configuration_known = false;
  capabilities.quick_transitions = false;
  capabilities.default_transition_state_available = false;
  capabilities.default_transition_configuration_available = false;
  capabilities.transition_completion_requires_video = false;
  capabilities.hotkey_trigger = false;
  capabilities.performance = false;
  capabilities.performance_metrics.clear();
  return info;
}
} // namespace studio_remote::teleprompter
