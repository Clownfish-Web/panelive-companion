#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/media/source_media_inspector.hpp>
#include <studio_remote/state/audio_monitoring_mode.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace studio_remote
{

struct Scene
{
  std::string id;
  std::string backend_id;
  std::string name;
  bool backend_identifier_preferred = false;
  bool is_program = false;
  bool is_preview = false;
  std::uint64_t item_revision = 0;
};

struct SceneItem
{
  std::int64_t id = 0;
  std::string stable_layer_id;
  std::string source_id;
  std::string source_name;
  std::string source_kind;
  std::string generic_kind;
  bool enabled = false;
  bool base = false;
  bool visibility_controllable = true;
  bool visibility_state_available = true;
  bool thumbnail_available = true;
  std::string thumbnail_unavailable_reason;
  std::uint32_t order = 0;
  bool group = false;
  bool nested_scene = false;
  bool audio_capable = false;
  SourceVisualCapability visual_capability = SourceVisualCapability::unknown;
  std::string color;
  std::string nested_scene_id;
  std::string container_name;
  std::uint32_t source_width = 0;
  std::uint32_t source_height = 0;
  std::string canvas;
};

struct AudioInput
{
  std::string id;
  std::string backend_uuid;
  std::string name;
  std::string kind = "unknown";
  bool backend_identifier_preferred = false;
  bool muted = false;
  bool solo = false;
  double balance = 0.0;
  bool balance_available = false;
  bool routed_to_master = false;
  bool master_routing_available = false;
  double volume_mul = 1.0;
  double volume_db = 0.0;
  AudioMonitoringMode monitoring_mode = AudioMonitoringMode::none;
  bool monitoring_mode_available = false;
  bool active_program = false;
  std::string meter_state = "stale";
  std::uint64_t last_meter_ns = 0;
  std::vector<float> peak_dbfs;
  std::vector<float> rms_dbfs;
  std::vector<float> input_peak_dbfs;
};

struct AudioMaster
{
  double volume_db = 0.0;
  bool muted = false;
};

struct SceneTransition
{
  std::string id;
  std::string backend_id;
  std::string name;
  std::string kind = "unknown";
  std::uint32_t order = 0;
  std::uint32_t duration_ms = 1'000;
  bool fixed_duration = false;
  bool configurable = false;
  bool fade_to_black = false;
};

struct StreamingDestination
{
  std::string id;
  std::string backend_id;
  std::string key;
  std::string label;
  std::string host;
  std::string service_kind = "custom";
  std::uint32_t order = 0;
  bool configured = false;
  bool enabled = false;
  std::string state = "stopped";
  std::string control_scope = "aggregate-only";
  std::string failure_reason;
  std::string platform_name;
};

class CommandStateSnapshot
{
public:
  std::uint64_t revision() const noexcept;
  bool fresh() const noexcept;
  bool studio_mode() const noexcept;
  std::uint32_t transition_duration_ms() const noexcept;
  std::string_view program_scene_id() const noexcept;
  std::string_view preview_scene_id() const noexcept;
  std::uint64_t program_frame_ns() const noexcept;
  bool transition_completion_requires_video() const noexcept;

  bool stream_active() const noexcept;
  bool stream_configured() const noexcept;
  std::optional<StreamingDestination> streaming_destination(std::string_view id) const;
  std::vector<StreamingDestination> streaming_destinations() const;
  bool record_active() const noexcept;
  bool record_paused() const noexcept;

  std::optional<Scene> scene(std::string_view id) const;
  std::optional<SceneItem> item(std::string_view scene_id, std::int64_t item_id) const;
  std::optional<AudioInput> input(std::string_view id) const;
  std::optional<AudioMaster> audio_master() const;
  std::optional<SceneTransition> transition(std::string_view id) const;
  std::size_t solo_input_count() const noexcept;

private:
  friend class StateStore;

  std::uint64_t revision_ = 0;
  bool fresh_ = false;
  bool studio_mode_ = false;
  std::uint32_t transition_duration_ms_ = 1'000;
  std::string program_scene_id_;
  std::string preview_scene_id_;
  std::uint64_t program_frame_ns_ = 0;
  bool transition_completion_requires_video_ = true;
  bool stream_active_ = false;
  bool stream_configured_ = false;
  std::unordered_map<std::string, StreamingDestination> streaming_destinations_;
  bool record_active_ = false;
  bool record_paused_ = false;
  std::unordered_map<std::string, Scene> scenes_;
  std::unordered_map<std::string, std::vector<SceneItem>> items_;
  std::unordered_map<std::string, std::vector<SceneItem>> group_items_;
  std::unordered_map<std::string, AudioInput> inputs_;
  std::unordered_map<std::string, SceneTransition> transitions_;
  std::optional<AudioMaster> audio_master_;
};

} // namespace studio_remote
