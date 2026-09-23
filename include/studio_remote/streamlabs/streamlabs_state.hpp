#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <studio_remote/provider/production_catalogs.hpp>
#include <vector>

namespace studio_remote::streamlabs
{

enum class OutputState
{
  offline,
  starting,
  active,
  stopping,
  reconnecting,
  error,
  unknown
};

struct SceneCollection
{
  std::string id;
  std::string name;
  bool operator==(const SceneCollection&) const = default;
};

struct SceneNode
{
  std::string id;
  std::string scene_id;
  std::string parent_id;
  std::string source_id;
  std::string name;
  std::string resource_id;
  std::string type;
  bool visible = false;
  std::string canvas;
  bool operator==(const SceneNode&) const = default;
};

struct Scene
{
  std::string id;
  std::string name;
  std::vector<SceneNode> nodes;
  bool operator==(const Scene&) const = default;
};

struct AudioSource
{
  std::string source_id;
  std::string name;
  std::string resource_id;
  bool muted = false;
  double volume_db = 0.0;
  double volume_multiplier = 1.0;
  int monitoring_type = -1;
  std::uint32_t audio_mixers = 0;
  bool operator==(const AudioSource&) const = default;
};

struct PerformanceState
{
  std::optional<double> cpu_usage_percent;
  std::optional<double> bandwidth_kbps;
  std::optional<double> frame_rate;
  std::optional<std::uint64_t> dropped_frames;
  std::optional<double> dropped_frames_percent;
  bool operator==(const PerformanceState&) const = default;
};

using CanvasDescriptor = ProductionCanvasDescriptor;
using CanvasState = ProductionCanvasState;
struct ConfiguredAction
{
  std::string id;
  std::string label;
  std::vector<std::vector<std::string>> bindings;
  bool operator==(const ConfiguredAction&) const = default;
};
struct Destination
{
  std::string id;
  std::string label;
  std::string platform;
  bool enabled = false;
  std::string service_kind = "custom";
  bool operator==(const Destination&) const = default;
};

struct State
{
  std::string desktop_version;
  SceneCollection active_collection;
  std::vector<Scene> scenes;
  std::string active_scene_id;
  std::vector<AudioSource> audio_sources;
  OutputState recording = OutputState::unknown;
  OutputState streaming = OutputState::unknown;
  bool studio_mode_enabled = false;
  std::string program_scene_id;
  std::string preview_scene_id;
  PerformanceState performance;
  std::optional<std::vector<SceneCollection>> collections;
  std::optional<std::vector<ConfiguredAction>> configured_actions;
  std::optional<std::vector<Destination>> destinations;
  std::optional<CanvasState> canvas;
  bool program_identity_available = true;
  bool operator==(const State&) const = default;
};

std::string_view output_state_name(OutputState state) noexcept;
bool output_state_active(OutputState state) noexcept;
const Scene* find_scene(const State& state, std::string_view id);
const SceneNode*
find_scene_node(const State& state, std::string_view scene_id, std::string_view node_id);
const AudioSource* find_audio_source(const State& state, std::string_view source_id);

} // namespace studio_remote::streamlabs
