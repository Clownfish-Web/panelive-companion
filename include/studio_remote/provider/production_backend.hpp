#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace studio_remote
{

struct ProductionBackendCapabilities
{
  bool preview = true;
  bool program_observation = true;
  bool system_hotkeys = true;
  bool scene_collections = false;
  bool program_selection = true;
  bool preview_selection = true;
  bool composition_monitor_render = true;
  bool native_audio_roster_required = true;
  bool layer_visibility = true;
  bool layer_colors = true;
  bool transition_catalog = true;
  bool transition_configure = true;
  bool transition_execute = true;
  bool program_video = true;
  bool preview_video = true;
  bool thumbnails = true;
  bool audio_meters = true;
  bool audio_monitoring = true;
  bool audio_mute = true;
  bool audio_volume = true;
  bool audio_input_positive_gain = false;
  bool audio_solo = false;
  bool audio_input_balance = false;
  bool audio_input_master_routing = false;
  bool audio_master_volume = false;
  bool audio_master_mute = false;
  bool audio_source_audition_bus = true;
  double audio_volume_minimum_db = -100.0;
  double audio_volume_maximum_db = 26.0;
  std::string audio_volume_fader_curve;
  std::uint32_t audio_volume_step_percent = 0;
  double audio_volume_fader_exponent = 1.0;
  bool recording_pause = true;
  bool recording_observe = true;
  bool recording_start_stop = true;
  bool streaming_observe = true;
  bool streaming_start_stop = true;
  bool streaming_destinations = true;
  bool streaming_multistream = false;
  bool streaming_destination_individual_control = false;
  std::uint32_t streaming_destination_maximum = 0;
  bool stream_configuration_known = true;
  bool quick_transitions = true;
  bool default_transition_uses_quick_play = false;
  bool default_transition_state_available = true;
  bool default_transition_configuration_available = true;
  bool transition_completion_requires_video = true;
  bool stable_command_identifiers_preferred = false;
  bool authoritative_control_verification = false;
  bool explicit_start_stop_for_record = false;
  bool explicit_start_stop_for_stream = false;
  bool hotkey_trigger = true;
  bool performance = true;
  std::vector<std::string> performance_metrics = {
      "system.cpuUsagePercent",
      "system.memoryUsageMiB",
      "system.availableDiskSpaceMiB",
      "render.framesPerSecond",
      "render.averageFrameTimeMs",
      "render.skippedFrames",
      "render.totalFrames",
      "encode.skippedFrames",
      "encode.totalFrames",
      "stream.congestionRatio",
      "stream.bytes",
      "stream.durationMs",
  };
};

enum class ProductionBackendKind
{
  production,
  companion_teleprompter
};

struct ProductionBackendInfo
{
  std::string id = "obs";
  std::string display_name = "OBS Studio";
  ProductionBackendCapabilities capabilities;
  ProductionBackendKind kind = ProductionBackendKind::production;
};

} // namespace studio_remote
