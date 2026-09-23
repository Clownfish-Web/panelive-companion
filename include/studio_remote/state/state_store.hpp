#pragma once
#include <studio_remote/common/types.hpp>
#include <studio_remote/media/audio_monitor.hpp>
#include <studio_remote/performance/production_performance.hpp>
#include <studio_remote/provider/production_catalogs.hpp>
#include <studio_remote/state/command_state_snapshot.hpp>
#include <studio_remote/state/source_membership.hpp>

#include <deque>

namespace studio_remote
{
struct SceneSource
{
  std::string id;
  std::string name;
  std::string kind;
  bool group = false;
  bool nested_scene = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t catalog_generation = 0;
};

struct LayerCommandTarget
{
  std::int64_t scene_item_id = 0;
  std::string resource_id;
  bool visibility_controllable = true;
  bool visibility_state_available = true;
};

class StateStore
{
public:
  using Listener = std::function<void(uint64_t, const json&)>;
  using AudioInputListener = std::function<void(const json&)>;
  StateStore(std::string instance_id, std::string name);

  void set_obs_connection(bool connected, std::string version = {});
  void set_gateway_state(std::string state);
  bool replace_baseline(const json& baseline,
                        ProductionCatalogSnapshot catalogs = {},
                        std::optional<std::uint64_t> observation_generation = std::nullopt);
  std::uint64_t source_observation_generation() const;
  ProductionCatalogSnapshot production_catalogs() const;
  void apply_obs_event(std::string_view type, const json& data);
  void set_video_health(std::string_view track, const json& health);
  void set_performance(ProductionPerformanceSample sample);
  void set_audio_source_audition_bus_enabled(bool enabled);

  json snapshot() const;
  json generic_state(Role role) const;
  json generic_capabilities(Role role,
                            std::uint64_t capability_revision,
                            bool thumbnails_available,
                            AudioMonitorStatus audio_monitor = {},
                            AudioMonitorStatus audio_master = {}) const;
  json generic_compositions(bool thumbnails_available = true) const;
  json generic_layers(std::string_view composition_id, bool thumbnails_available = true) const;
  json generic_transitions() const;
  json generic_audio_inputs() const;
  bool streaming_destinations_available() const;
  json generic_streaming_destinations() const;
  std::optional<json> generic_performance() const;
  json scene_list() const;
  json scene_items(std::string_view scene_id) const;
  json group_items(std::string_view scene_id, int64_t group_item_id) const;
  json audio_inputs() const;
  std::optional<std::vector<json>> deltas_after(uint64_t revision) const;
  std::optional<json> meter_frame(bool mixer_open);
  CommandStateSnapshot command_state_snapshot() const;
  uint64_t revision() const;
  bool fresh() const;
  bool studio_mode() const;
  std::uint32_t transition_duration_ms() const;
  std::optional<std::string> transition_name(std::string_view transition_id) const;
  std::optional<SceneTransition> transition(std::string_view transition_id) const;
  bool current_transition(std::string_view transition_id) const;
  std::optional<Scene> scene(std::string_view id) const;
  std::optional<SceneItem> item(std::string_view scene_id, int64_t item_id) const;
  std::optional<std::int64_t> scene_item_id(std::string_view composition_id,
                                            std::string_view layer_id) const;
  std::optional<LayerCommandTarget> layer_command_target(std::string_view composition_id,
                                                         std::string_view layer_id) const;
  std::optional<SceneSource> source(std::string_view source_id) const;
  std::optional<AudioInput> input(std::string_view id) const;
  std::size_t solo_input_count() const noexcept;
  bool audio_input_monitoring_available() const;
  bool audio_source_audition_bus_enabled() const;
  void subscribe(Listener listener);
  void subscribe_audio_inputs(AudioInputListener listener);

private:
  void changed_locked(json changed);
  json snapshot_locked() const;
  std::vector<SourceOccurrence> read_source_occurrences(const json& values,
                                                        const std::string& container_id,
                                                        std::size_t depth = 0) const;
  json source_membership_locked(std::string_view composition_id) const;
  json apply_scene_visibility_locked(const json& data);
  SourceMembership source_membership_;
  std::uint64_t source_observation_generation_ = 0;

  mutable std::recursive_mutex mutex_;
  std::string instance_id_, name_, gateway_state_ = "starting", obs_version_;
  std::string backend_id_ = "obs", system_name_ = "OBS Studio";
  ProductionCatalogSnapshot production_catalogs_;
  bool obs_connected_ = false, fresh_ = false, studio_mode_ = false;
  bool audio_source_audition_bus_enabled_ = false;
  uint64_t revision_ = 0, meter_sequence_ = 0, performance_sequence_ = 0,
           source_catalog_generation_ = 0;
  std::uint32_t transition_duration_ms_ = 1'000;
  std::string program_scene_id_, preview_scene_id_;
  std::string current_transition_id_, current_transition_name_,
      current_transition_kind_ = "unknown";
  bool current_transition_fixed_ = false;
  bool current_transition_configurable_ = false;
  bool layer_visibility_available_ = true;
  bool layer_colors_available_ = true;
  bool preview_available_ = true;
  bool program_selection_available_ = true;
  bool preview_selection_available_ = true;
  bool composition_monitor_render_available_ = true;
  bool transition_catalog_available_ = true;
  bool transition_configure_available_ = true;
  bool transition_execute_available_ = true;
  bool program_video_supported_ = true;
  bool preview_video_supported_ = true;
  bool thumbnails_supported_ = true;
  bool audio_meters_available_ = true;
  bool audio_monitoring_backend_available_ = true;
  bool audio_mute_available_ = true;
  bool audio_volume_available_ = true;
  bool audio_input_positive_gain_available_ = false;
  bool audio_solo_available_ = false;
  bool audio_input_balance_available_ = false;
  bool audio_input_master_routing_available_ = false;
  bool audio_master_volume_available_ = false;
  bool audio_master_mute_available_ = false;
  bool audio_source_audition_bus_available_ = true;
  bool performance_supported_ = true;
  std::vector<std::string> performance_metrics_;
  double audio_volume_minimum_db_ = -100.0;
  double audio_volume_maximum_db_ = 26.0;
  std::string audio_volume_fader_curve_;
  bool recording_pause_supported_ = true;
  bool recording_pause_available_ = true;
  bool recording_observe_available_ = true;
  bool recording_start_stop_available_ = true;
  bool streaming_observe_available_ = true;
  bool streaming_start_stop_available_ = true;
  bool streaming_destinations_available_ = false;
  bool streaming_multistream_available_ = false;
  bool streaming_destination_individual_control_ = false;
  std::uint32_t streaming_destination_maximum_ = 0;
  bool stream_configuration_known_ = true;
  bool quick_transitions_available_ = true;
  bool default_transition_uses_quick_play_ = false;
  bool default_transition_state_available_ = true;
  bool default_transition_configuration_available_ = true;
  bool transition_completion_requires_video_ = true;
  bool stable_command_identifiers_preferred_ = false;
  json stream_ = {{"state", "stopped"}, {"active", false}, {"configured", false}};
  std::unordered_map<std::string, StreamingDestination> streaming_destinations_;
  json record_ = {{"state", "stopped"},
                  {"active", false},
                  {"paused", false},
                  {"timecode", "00:00:00.000"},
                  {"durationMs", 0},
                  {"observedAtMonotonicNs", 0}};
  json video_ = {{"program", {{"available", false}, {"trackEpoch", 0}, {"ageMs", nullptr}}},
                 {"preview", {{"available", false}, {"trackEpoch", 0}, {"ageMs", nullptr}}}};
  std::optional<ProductionPerformanceSample> performance_;
  std::uint64_t performance_observed_at_ns_ = 0;
  std::unordered_map<std::string, Scene> scenes_;
  // Provider UI order, independent of the identity lookup map and mutable scene names.
  std::vector<std::string> scene_order_;
  std::unordered_map<std::string, SceneTransition> transitions_;
  std::unordered_map<std::string, std::vector<SceneItem>> items_;
  std::unordered_map<std::string, std::vector<SceneItem>> group_items_;
  std::unordered_map<std::string, AudioInput> inputs_;
  std::optional<AudioMaster> audio_master_;
  std::vector<Listener> listeners_;
  std::vector<AudioInputListener> audio_input_listeners_;
  std::deque<json> revision_journal_;
};
} // namespace studio_remote
