#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <studio_remote/state/recording_state.hpp>
#include <studio_remote/state/recording_timecode.hpp>
#include <studio_remote/state/state_store.hpp>
#include <unordered_set>

namespace studio_remote
{
namespace
{
constexpr std::size_t revision_journal_capacity = 256;
constexpr std::uint64_t layer_id_mask = 0x9e3779b97f4a7c15ULL;
constexpr double minimum_obs_volume_db = -100.0;
constexpr double maximum_obs_volume_db = 26.0;
constexpr std::uint32_t minimum_transition_duration_ms = 50;
constexpr std::uint32_t maximum_transition_duration_ms = 20'000;
constexpr std::size_t maximum_streaming_destinations = 32;
constexpr std::size_t maximum_streaming_destination_id_bytes = 128;
constexpr std::size_t maximum_streaming_destination_label_bytes = 256;
constexpr std::size_t maximum_streaming_destination_host_bytes = 256;
constexpr std::size_t maximum_streaming_destination_kind_bytes = 64;

std::string required_resource_id(std::string_view id)
{
  if (id.empty())
    throw std::invalid_argument("Production resource is missing its stable identity");
  return std::string(id);
}

std::string generic_layer_id(std::int64_t item_id)
{
  char value[32]{};
  const auto opaque = static_cast<std::uint64_t>(item_id) ^ layer_id_mask;
  std::snprintf(value, sizeof(value), "layer-%016llx", static_cast<unsigned long long>(opaque));
  return value;
}

std::string generic_layer_id(const SceneItem& item)
{
  if (item.stable_layer_id.empty())
    return generic_layer_id(item.id);
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char byte : item.stable_layer_id)
  {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  char value[32]{};
  std::snprintf(value, sizeof(value), "layer-%016llx", static_cast<unsigned long long>(hash));
  return value;
}

std::optional<std::uint32_t> rgb_color(std::string_view native_color)
{
  if (native_color.size() != 7 && native_color.size() != 9)
    return std::nullopt;
  const auto rgb = native_color.substr(native_color.size() - 6);
  std::uint32_t value = 0;
  const auto [end, error] = std::from_chars(rgb.data(), rgb.data() + rgb.size(), value, 16);
  if (error != std::errc{} || end != rgb.data() + rgb.size())
    return std::nullopt;
  return value;
}

std::string generic_layer_kind(const SceneItem& item)
{
  if (!item.generic_kind.empty())
    return item.generic_kind;
  if (item.group)
    return "group";
  if (item.visual_capability == SourceVisualCapability::audio_only)
    return "audio";

  constexpr std::array<std::string_view, 10> audio_only_kinds{
      "wasapi_input_capture",
      "wasapi_output_capture",
      "wasapi_process_output_capture",
      "coreaudio_input_capture",
      "coreaudio_output_capture",
      "pulse_input_capture",
      "pulse_output_capture",
      "alsa_input_capture",
      "jack_output_capture",
      "application_audio_capture",
  };
  if (std::ranges::find(audio_only_kinds, item.source_kind) != audio_only_kinds.end())
    return "audio";

  if (item.source_kind == "browser_source")
    return "browser";

  constexpr std::array<std::string_view, 6> image_kinds{
      "image_source",
      "image_source_v2",
      "color_source",
      "color_source_v2",
      "color_source_v3",
      "slideshow",
  };
  if (std::ranges::find(image_kinds, item.source_kind) != image_kinds.end())
    return "image";

  constexpr std::array<std::string_view, 4> text_kinds{
      "text_gdiplus",
      "text_gdiplus_v2",
      "text_ft2_source",
      "text_ft2_source_v2",
  };
  if (std::ranges::find(text_kinds, item.source_kind) != text_kinds.end())
    return "text";

  if (item.visual_capability == SourceVisualCapability::visual)
    return "video";

  constexpr std::array<std::string_view, 10> visual_video_kinds{
      "dshow_input",
      "av_capture_input",
      "v4l2_input",
      "decklink-input",
      "monitor_capture",
      "window_capture",
      "game_capture",
      "xcomposite_input",
      "pipewire-screen-capture-source",
      "screen_capture",
  };
  if (std::ranges::find(visual_video_kinds, item.source_kind) != visual_video_kinds.end())
    return "video";

  return "unknown";
}

SourceVisualCapability parse_visual_capability(std::string_view value)
{
  if (value == "visual")
    return SourceVisualCapability::visual;
  if (value == "audio-only")
    return SourceVisualCapability::audio_only;
  return SourceVisualCapability::unknown;
}

std::string generic_audio_kind(std::string_view native_kind)
{
  if (native_kind.find("output_capture") != std::string::npos)
    return "speaker";
  if (native_kind.find("input_capture") != std::string::npos ||
      native_kind.find("mic") != std::string::npos)
    return "microphone";
  if (native_kind.find("media") != std::string::npos ||
      native_kind.find("ffmpeg") != std::string::npos ||
      native_kind.find("vlc") != std::string::npos)
    return "media";
  return "unknown";
}

std::optional<double> number_field(const json& value, std::string_view key)
{
  const auto found = value.find(std::string(key));
  if (found == value.end() || !found->is_number())
    return std::nullopt;
  const auto number = found->get<double>();
  return std::isfinite(number) ? std::optional(number) : std::nullopt;
}

std::string
stable_transition_id(std::string_view uuid, std::string_view name, std::string_view kind)
{
  if (!uuid.empty())
    return std::string(uuid);

  const std::string material = "transition:" + std::string(kind) + ":" + std::string(name);
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char byte : material)
  {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  char value[40]{};
  std::snprintf(value, sizeof(value), "transition-%016llx", static_cast<unsigned long long>(hash));
  return value;
}

std::string normalize_transition_kind(std::string_view native_kind)
{
  if (native_kind.find("cut") != std::string::npos)
    return "cut";
  if (native_kind.find("fade") != std::string::npos)
    return "fade";
  if (native_kind.find("swipe") != std::string::npos)
    return "swipe";
  if (native_kind.find("slide") != std::string::npos)
    return "slide";
  if (native_kind.find("stinger") != std::string::npos)
    return "stinger";
  if (native_kind.find("luma") != std::string::npos)
    return "luma-wipe";
  if (native_kind.find("shader") != std::string::npos)
    return "shader";
  return native_kind.empty() ? "unknown" : "custom";
}

double normalized_obs_volume_db(const json& value)
{
  if (const auto volume_db = number_field(value, "inputVolumeDb"))
    return std::clamp(*volume_db, minimum_obs_volume_db, maximum_obs_volume_db);

  if (const auto volume_mul = number_field(value, "inputVolumeMul"))
  {
    if (*volume_mul <= 0.0)
      return minimum_obs_volume_db;
    return std::clamp(20.0 * std::log10(*volume_mul), minimum_obs_volume_db, maximum_obs_volume_db);
  }

  return 0.0;
}

json generic_layer(const SceneItem& item)
{
  json value = {{"layerId", generic_layer_id(item)},
                {"resourceId", item.source_id},
                {"name", item.source_name},
                {"kind", generic_layer_kind(item)},
                {"visible", item.enabled},
                {"pending", false},
                {"base", item.base},
                {"order", item.order},
                {"zIndex", item.order},
                {"visibilityControllable", item.visibility_controllable},
                {"visibilityStateAvailable", item.visibility_state_available},
                {"thumbnail",
                 !item.thumbnail_available ? json{{"available", false},
                                                  {"reason",
                                                   item.thumbnail_unavailable_reason.empty()
                                                       ? "backend-unavailable"
                                                       : item.thumbnail_unavailable_reason}}
                 : item.group ? json{{"available", false}, {"reason", "backend-unavailable"}}
                              : json{{"available", true},
                                     {"endpoint", "/v1/resources/" + item.source_id + "/thumbnail"},
                                     {"cacheToken", "resource-" + item.source_id}}}};
  if (!item.canvas.empty())
    value["canvasId"] = item.canvas;
  if (const auto color = rgb_color(item.color))
    value["backendColor"] = *color;
  return value;
}

json capability(std::string access, bool available, std::string reason = {}, json limits = json())
{
  json value = {{"access", std::move(access)},
                {"availability", available ? "available" : "temporarily-unavailable"}};
  if (!available)
    value["reason"] = reason.empty() ? "backend-unavailable" : std::move(reason);
  if (!limits.is_null())
    value["limits"] = std::move(limits);
  return value;
}

bool valid_ascii_token(std::string_view value)
{
  return std::all_of(value.begin(),
                     value.end(),
                     [](unsigned char character)
                     {
                       return std::isalnum(character) || character == '-' || character == '_' ||
                              character == '.';
                     });
}

bool valid_destination_state(std::string_view value)
{
  constexpr std::array states{
      "stopped", "starting", "streaming", "reconnecting", "stopping", "error", "unknown"};
  return std::ranges::find(states, value) != states.end();
}

bool valid_destination_failure(std::string_view value)
{
  constexpr std::array reasons{"authentication",
                               "configuration",
                               "network",
                               "rate-limited",
                               "provider-error",
                               "resource-stale",
                               "unknown"};
  return std::ranges::find(reasons, value) != reasons.end();
}

bool destination_active(const StreamingDestination& destination)
{
  return destination.state == "streaming" || destination.state == "reconnecting";
}

json streaming_destination_json(const StreamingDestination& destination)
{
  json value = {
      {"destinationId", destination.id},
      {"key", destination.key},
      {"displayName", destination.label},
      {"label", destination.label},
      {"host", destination.host},
      {"order", destination.order},
      {"configured", destination.configured},
      {"enabled", destination.enabled},
      {"state", destination.state},
      {"controlScope", destination.control_scope},
      {"failureReason",
       destination.failure_reason.empty() ? json(nullptr) : json(destination.failure_reason)}};
  if (!destination.platform_name.empty())
    value["platformName"] = destination.platform_name;
  if (!destination.service_kind.empty())
    value["serviceKind"] = destination.service_kind;
  return value;
}

std::vector<StreamingDestination>
ordered_destinations(const std::unordered_map<std::string, StreamingDestination>& destinations)
{
  std::vector<StreamingDestination> values;
  values.reserve(destinations.size());
  for (const auto& [id, destination] : destinations)
  {
    static_cast<void>(id);
    values.push_back(destination);
  }
  std::sort(values.begin(),
            values.end(),
            [](const StreamingDestination& left, const StreamingDestination& right)
            { return left.order != right.order ? left.order < right.order : left.id < right.id; });
  return values;
}

std::string streaming_output_state(const json& stream)
{
  auto state = stream.value("state", "stopped");
  if (state == "started")
    state = "streaming";
  if (state != "stopped" && state != "starting" && state != "streaming" && state != "stopping" &&
      state != "error")
    state = stream.value("active", false) ? "streaming" : "stopped";
  return state;
}

json streaming_destination_aggregate(const std::vector<StreamingDestination>& destinations,
                                     std::string_view output_state)
{
  std::uint32_t enabled = 0;
  std::uint32_t active = 0;
  std::uint32_t failed = 0;
  bool any_enabled_non_active = false;
  bool delivery_unknown = false;

  for (const auto& destination : destinations)
  {
    if (!destination.enabled)
      continue;
    ++enabled;
    if (destination_active(destination))
      ++active;
    else
      any_enabled_non_active = true;
    if (destination.state == "unknown" && destination.failure_reason.empty())
      delivery_unknown = true;
    else if (destination.state == "error" || destination.state == "unknown")
      ++failed;
  }

  std::string health = "idle";
  if (delivery_unknown)
    health = "unknown";
  else if (enabled > 0 && active == enabled)
    health = "full";
  else if (active > 0 && any_enabled_non_active)
    health = "partial";
  else if (active == 0 && failed > 0)
    health = "failed";

  return {{"configured", enabled > 0},
          {"state", output_state},
          {"enabledDestinationCount", enabled},
          {"activeDestinationCount", active},
          {"failedDestinationCount", failed},
          {"health", health}};
}

std::unordered_map<std::string, StreamingDestination>
parse_streaming_destinations(const json& destinations, bool individual_control_available)
{
  if (!destinations.is_array() || destinations.size() > maximum_streaming_destinations)
    throw std::runtime_error("Streaming destination roster is invalid");

  std::unordered_set<std::string> ids;
  std::unordered_map<std::string, StreamingDestination> result;
  std::uint32_t fallback_order = 0;
  for (const auto& value : destinations)
  {
    if (!value.is_object())
      throw std::runtime_error("Streaming destination entry is invalid");
    StreamingDestination destination;
    destination.backend_id = value.value("backendId", "");
    destination.id = value.value("destinationId", destination.backend_id);
    destination.key = value.value("key", destination.id);
    destination.label = value.value("label", value.value("displayName", ""));
    destination.host = value.value("host", "");
    destination.platform_name = value.value("platformName", "");
    destination.service_kind = value.value("serviceKind", "custom");
    destination.order = value.value("order", fallback_order);
    destination.configured = value.value("configured", false);
    destination.enabled = value.value("enabled", destination.configured);
    destination.state = value.value("state", "stopped");
    destination.control_scope =
        value.value("controlScope", individual_control_available ? "individual" : "aggregate-only");
    if (value.contains("failureReason") && value["failureReason"].is_string())
      destination.failure_reason = value["failureReason"].get<std::string>();
    if (destination.id.empty() || destination.id.size() > maximum_streaming_destination_id_bytes ||
        destination.key.empty() ||
        destination.key.size() > maximum_streaming_destination_label_bytes ||
        !ids.insert(destination.id).second || destination.label.empty() ||
        destination.label.size() > maximum_streaming_destination_label_bytes ||
        destination.host.empty() ||
        destination.host.size() > maximum_streaming_destination_host_bytes ||
        destination.service_kind.size() > maximum_streaming_destination_kind_bytes ||
        !valid_ascii_token(destination.service_kind) ||
        !valid_destination_state(destination.state) ||
        (destination.control_scope != "individual" &&
         destination.control_scope != "aggregate-only") ||
        (!destination.failure_reason.empty() &&
         !valid_destination_failure(destination.failure_reason)))
      throw std::runtime_error("Streaming destination roster is invalid");
    if (destination.state != "error" && destination.state != "unknown")
      destination.failure_reason.clear();
    result.emplace(destination.id, std::move(destination));
    ++fallback_order;
  }
  return result;
}

void apply_stream_configuration(
    json& stream, const std::unordered_map<std::string, StreamingDestination>& destinations)
{
  // Configuration cannot replace the independently observed output lifecycle. In particular,
  // unknown destination activity does not prove that the provider stopped transmitting.
  stream["configured"] =
      std::ranges::any_of(destinations, [](const auto& entry) { return entry.second.enabled; });
}

std::uint32_t source_dimension(const json& item, std::string_view key)
{
  const auto transform = item.find("sceneItemTransform");
  if (transform == item.end() || !transform->is_object())
    return 0;
  const auto value = transform->find(std::string(key));
  if (value == transform->end() || !value->is_number() || value->get<double>() <= 0)
    return 0;
  return static_cast<std::uint32_t>(value->get<double>());
}

json thumbnail_descriptor(const SceneItem& item)
{
  if (item.group)
    return {{"available", false}, {"reason", "unsupported_source"}};
  return {{"available", true},
          {"endpoint", "/v1/sources/" + item.source_id + "/thumbnail"},
          {"defaultMaxWidth", 192},
          {"defaultMaxHeight", 108}};
}

json scene_item_resource(const SceneItem& item)
{
  return {{"sceneItemId", item.id},
          {"sourceId", item.source_id},
          {"sourceName", item.source_name},
          {"sourceKind", item.source_kind},
          {"enabled", item.enabled},
          {"isGroup", item.group},
          {"isNestedScene", item.nested_scene},
          {"audioCapable", item.audio_capable},
          {"color", item.color.empty() ? json(nullptr) : json(item.color)},
          {"sceneId", item.nested_scene ? json(item.nested_scene_id) : json(nullptr)},
          {"thumbnail", thumbnail_descriptor(item)}};
}
} // namespace

StateStore::StateStore(std::string instance_id, std::string name)
    : instance_id_(std::move(instance_id)), name_(std::move(name))
{
}


void StateStore::changed_locked(json changed)
{
  ++revision_;
  revision_journal_.push_back(
      {{"type", "stateDelta"}, {"revision", revision_}, {"changed", changed}});
  if (revision_journal_.size() > revision_journal_capacity)
    revision_journal_.pop_front();
  for (const auto& listener : listeners_)
    listener(revision_, changed);
}

void StateStore::set_obs_connection(bool connected, std::string version)
{
  std::lock_guard lock(mutex_);
  ++source_observation_generation_;
  obs_connected_ = connected;
  if (!version.empty())
    obs_version_ = std::move(version);
  if (!connected)
  {
    fresh_ = false;
    gateway_state_ = "obs-reconnecting";
    for (auto& [_, input] : inputs_)
      input.meter_state = "stale";
    for (auto track : {"program", "preview"})
    {
      const auto epoch = video_[track].value("trackEpoch", 0U);
      video_[track] = {{"available", false}, {"trackEpoch", epoch}, {"ageMs", nullptr}};
    }
  }
  changed_locked({{"obs.connected", connected}, {"gatewayState", gateway_state_}});
}

void StateStore::set_gateway_state(std::string state)
{
  std::lock_guard lock(mutex_);
  if (gateway_state_ == state)
    return;
  gateway_state_ = std::move(state);
  changed_locked({{"gatewayState", gateway_state_}});
}

void StateStore::set_audio_source_audition_bus_enabled(bool enabled)
{
  std::lock_guard lock(mutex_);
  if (audio_source_audition_bus_enabled_ == enabled)
    return;
  audio_source_audition_bus_enabled_ = enabled;
  changed_locked({{"audioSourceAuditionBus.enabled", enabled}});
}

ProductionCatalogSnapshot StateStore::production_catalogs() const
{
  std::lock_guard lock(mutex_);
  auto result = production_catalogs_;
  result.state_sequence = revision_;
  result.production_provider = backend_id_;
  if (!obs_connected_ || !fresh_)
  {
    result.canvas.reset();
    result.collections.reset();
  }
  return result;
}

std::uint64_t StateStore::source_observation_generation() const
{
  std::lock_guard lock(mutex_);
  return source_observation_generation_;
}

bool StateStore::replace_baseline(const json& b,
                                  ProductionCatalogSnapshot catalogs,
                                  std::optional<std::uint64_t> observation_generation)
{
  std::lock_guard lock(mutex_);
  // Network reads are outside this mutex. An intervening graph event invalidates
  // the read transaction, including edits in a child of another composition.
  if (observation_generation && *observation_generation != source_observation_generation_)
    return false;
  fresh_ = false;
  production_catalogs_ = std::move(catalogs);
  production_catalogs_.baseline_sequence = revision_ + 1;
  backend_id_ = b.value("backendId", "obs");
  system_name_ = b.value("systemName", "OBS Studio");
  const auto backend_capabilities = b.value("backendCapabilities", json::object());
  layer_visibility_available_ = backend_capabilities.value("layerVisibility", true);
  layer_colors_available_ = backend_capabilities.value("layerColors", true);
  preview_available_ = backend_capabilities.value("preview", true);
  program_selection_available_ = backend_capabilities.value("programSelection", true);
  preview_selection_available_ = backend_capabilities.value("previewSelection", true);
  composition_monitor_render_available_ =
      backend_capabilities.value("compositionMonitorRender", true);
  transition_catalog_available_ = backend_capabilities.value("transitionCatalog", true);
  transition_configure_available_ = backend_capabilities.value("transitionConfigure", true);
  transition_execute_available_ = backend_capabilities.value("transitionExecute", true);
  program_video_supported_ = backend_capabilities.value("programVideo", true);
  preview_video_supported_ = backend_capabilities.value("previewVideo", true);
  thumbnails_supported_ = backend_capabilities.value("thumbnails", true);
  audio_meters_available_ = backend_capabilities.value("audioMeters", true);
  audio_monitoring_backend_available_ = backend_capabilities.value("audioMonitoring", true);
  audio_mute_available_ = backend_capabilities.value("audioMute", true);
  audio_volume_available_ = backend_capabilities.value("audioVolume", true);
  audio_input_positive_gain_available_ =
      backend_capabilities.value("audioInputPositiveGain", false);
  audio_solo_available_ = backend_capabilities.value("audioSolo", false);
  audio_input_balance_available_ = backend_capabilities.value("audioInputBalance", false);
  audio_input_master_routing_available_ =
      backend_capabilities.value("audioInputMasterRouting", false);
  audio_master_volume_available_ = backend_capabilities.value("audioMasterVolume", false);
  audio_master_mute_available_ = backend_capabilities.value("audioMasterMute", false);
  audio_source_audition_bus_available_ = backend_capabilities.value("audioSourceAuditionBus", true);
  performance_supported_ = backend_capabilities.value("performance", true);
  performance_metrics_ =
      backend_capabilities.value("performanceMetrics", std::vector<std::string>{});
  audio_volume_minimum_db_ = backend_capabilities.value("audioVolumeMinimumDb", -100.0);
  audio_volume_maximum_db_ = backend_capabilities.value("audioVolumeMaximumDb", 26.0);
  audio_volume_fader_curve_ = backend_capabilities.value("audioVolumeFaderCurve", "");
  recording_pause_supported_ = backend_capabilities.value("recordingPause", true);
  recording_pause_available_ =
      backend_capabilities.value("recordingPauseAvailable", recording_pause_supported_);
  recording_observe_available_ = backend_capabilities.value("recordingObserve", true);
  recording_start_stop_available_ = backend_capabilities.value("recordingStartStop", true);
  streaming_observe_available_ = backend_capabilities.value("streamingObserve", true);
  streaming_start_stop_available_ = backend_capabilities.value("streamingStartStop", true);
  streaming_destinations_available_ = backend_capabilities.value("streamingDestinations", false);
  streaming_multistream_available_ = backend_capabilities.value("streamingMultistream", false);
  streaming_destination_individual_control_ =
      backend_capabilities.value("streamingDestinationIndividualControl", false);
  streaming_destination_maximum_ = backend_capabilities.value("streamingDestinationMaximum", 0U);
  stream_configuration_known_ = backend_capabilities.value("streamConfigurationKnown", true);
  quick_transitions_available_ = backend_capabilities.value("quickTransitions", true);
  default_transition_uses_quick_play_ =
      backend_capabilities.value("defaultTransitionUsesQuickPlay", false);
  default_transition_state_available_ =
      backend_capabilities.value("defaultTransitionStateAvailable", true);
  default_transition_configuration_available_ =
      backend_capabilities.value("defaultTransitionConfigurationAvailable", true);
  transition_completion_requires_video_ =
      backend_capabilities.value("transitionCompletionRequiresVideo", true);
  stable_command_identifiers_preferred_ =
      backend_capabilities.value("stableCommandIdentifiersPreferred", false);
  ++source_catalog_generation_;
  scenes_.clear();
  scene_order_.clear();
  items_.clear();
  group_items_.clear();
  source_membership_.clear();
  transitions_.clear();
  studio_mode_ = b.value("studioMode", false);
  transition_duration_ms_ = std::clamp(b.value("transitionDurationMs", 1'000U),
                                       minimum_transition_duration_ms,
                                       maximum_transition_duration_ms);
  current_transition_name_ = b.value("currentTransitionName", "");
  current_transition_kind_ = normalize_transition_kind(b.value("currentTransitionKind", ""));
  current_transition_fixed_ = b.value("currentTransitionFixed", false);
  current_transition_configurable_ = b.value("currentTransitionConfigurable", false);
  obs_version_ = b.value("obsVersion", obs_version_);
  std::uint32_t transition_order = 0;
  for (const auto& value : b.value("transitions", json::array()))
  {
    SceneTransition transition;
    transition.name = value.value("transitionName", "");
    if (transition.name.empty())
      transition.name = value.value("name", "");
    const auto native_kind = value.value("transitionKind", "");
    transition.kind = normalize_transition_kind(native_kind);
    transition.backend_id = value.value("transitionUuid", "");
    transition.id = value.value("quickTransitionId", "");
    if (transition.id.empty())
      transition.id = stable_transition_id(transition.backend_id, transition.name, native_kind);
    transition.order = transition_order++;
    transition.duration_ms = value.value("transitionDuration", transition_duration_ms_);
    transition.fixed_duration = value.value("transitionFixed", false);
    transition.configurable = value.value("transitionConfigurable", false);
    transition.fade_to_black = value.value("fadeToBlack", false);
    if (!transition.name.empty())
      transitions_.emplace(transition.id, std::move(transition));
  }
  current_transition_id_ = stable_transition_id(b.value("currentTransitionUuid", ""),
                                                current_transition_name_,
                                                b.value("currentTransitionKind", ""));
  const auto current_transition_uuid = b.value("currentTransitionUuid", "");
  const auto matching_current =
      std::find_if(transitions_.begin(),
                   transitions_.end(),
                   [&](const auto& entry)
                   {
                     return current_transition_uuid.empty()
                                ? entry.second.name == current_transition_name_
                                : entry.second.backend_id == current_transition_uuid;
                   });
  if (matching_current != transitions_.end())
    current_transition_id_ = matching_current->first;
  if (!current_transition_name_.empty() && !transitions_.contains(current_transition_id_))
  {
    transitions_.emplace(current_transition_id_,
                         SceneTransition{current_transition_id_,
                                         b.value("currentTransitionUuid", ""),
                                         current_transition_name_,
                                         current_transition_kind_,
                                         transition_order,
                                         transition_duration_ms_,
                                         current_transition_fixed_,
                                         current_transition_configurable_});
  }
  const auto program_name = b.value("programSceneName", "");
  const auto preview_name = b.value("previewSceneName", "");
  const auto program_backend_id =
      backend_id_ == "streamlabs" ? b.value("programSceneUuid", "") : "";
  const auto preview_backend_id =
      backend_id_ == "streamlabs" ? b.value("previewSceneUuid", "") : "";
  program_scene_id_.clear();
  preview_scene_id_.clear();
  for (const auto& value : b.value("scenes", json::array()))
  {
    Scene scene;
    scene.name = value.value("sceneName", "");
    scene.backend_id = value.value("sceneUuid", "");
    scene.backend_identifier_preferred = stable_command_identifiers_preferred_;
    scene.id = required_resource_id(scene.backend_id);
    scene.is_program = !program_backend_id.empty() ? scene.backend_id == program_backend_id
                                                   : scene.name == program_name;
    scene.is_preview =
        studio_mode_ && (!preview_backend_id.empty() ? scene.backend_id == preview_backend_id
                                                     : scene.name == preview_name);
    scene.item_revision = revision_ + 1;
    if (scene.is_program)
      program_scene_id_ = scene.id;
    if (scene.is_preview)
      preview_scene_id_ = scene.id;
    scenes_.emplace(scene.id, scene);
    scene_order_.push_back(scene.id);
    if (backend_id_ == "obs")
      source_membership_.replace(
          scene.id,
          read_source_occurrences(value.value("sceneItems", json::array()), scene.backend_id));

    std::vector<SceneItem> scene_items;
    for (const auto& v : value.value("sceneItems", json::array()))
    {
      SceneItem item;
      item.id = v.value("sceneItemId", 0LL);
      item.stable_layer_id = v.value("stableLayerId", "");
      item.canvas = v.value("canvas", "");
      item.source_name = v.value("sourceName", "");
      item.source_id = required_resource_id(v.value("sourceUuid", ""));
      item.source_kind = v.value("inputKind", v.value("sourceType", "unknown"));
      item.generic_kind = v.value("genericKind", "");
      item.enabled = v.value("sceneItemEnabled", false);
      item.base = v.value("baseLayer", false);
      item.visibility_controllable = v.value("visibilityControllable", true);
      item.visibility_state_available = v.value("visibilityStateAvailable", true);
      item.thumbnail_available = v.value("thumbnailAvailable", true);
      item.thumbnail_unavailable_reason = v.value("thumbnailUnavailableReason", "");
      item.order = v.value("layerOrder", static_cast<std::uint32_t>(scene_items.size()));
      item.group = v.value("isGroup", false);
      item.nested_scene = v.value("sourceType", "") == "OBS_SOURCE_TYPE_SCENE";
      if (item.nested_scene)
        item.nested_scene_id = required_resource_id(v.value("sourceUuid", ""));
      item.audio_capable = v.value("audioCapable", false);
      item.visual_capability = parse_visual_capability(v.value("visualCapability", "unknown"));
      item.color =
          v.contains("color") && v["color"].is_string() ? v["color"].get<std::string>() : "";
      item.container_name = scene.name;
      item.source_width = source_dimension(v, "sourceWidth");
      item.source_height = source_dimension(v, "sourceHeight");
      if (item.group)
      {
        std::vector<SceneItem> children;
        for (const auto& child_value : v.value("groupSceneItems", json::array()))
        {
          SceneItem child;
          child.id = child_value.value("sceneItemId", 0LL);
          child.source_name = child_value.value("sourceName", "");
          child.source_id = required_resource_id(child_value.value("sourceUuid", ""));
          child.source_kind =
              child_value.value("inputKind", child_value.value("sourceType", "unknown"));
          child.enabled = child_value.value("sceneItemEnabled", false);
          child.group = child_value.value("isGroup", false);
          child.nested_scene = child_value.value("sourceType", "") == "OBS_SOURCE_TYPE_SCENE";
          child.audio_capable = child_value.value("audioCapable", false);
          child.visual_capability =
              parse_visual_capability(child_value.value("visualCapability", "unknown"));
          child.color = child_value.contains("color") && child_value["color"].is_string()
                            ? child_value["color"].get<std::string>()
                            : "";
          child.container_name = item.source_name;
          child.source_width = source_dimension(child_value, "sourceWidth");
          child.source_height = source_dimension(child_value, "sourceHeight");
          if (child.nested_scene)
            child.nested_scene_id = required_resource_id(child_value.value("sourceUuid", ""));
          children.push_back(std::move(child));
        }
        group_items_[scene.id + ":" + std::to_string(item.id)] = std::move(children);
      }
      scene_items.push_back(std::move(item));
    }
    items_.emplace(scene.id, std::move(scene_items));
  }
  std::unordered_map<std::string, AudioInput> replacement_inputs;
  for (const auto& v : b.value("inputs", json::array()))
  {
    AudioInput input;
    input.name = v.value("inputName", "");
    input.backend_uuid = v.value("inputUuid", "");
    input.id = required_resource_id(input.backend_uuid);
    input.kind = generic_audio_kind(v.value("inputKind", ""));
    input.backend_identifier_preferred = stable_command_identifiers_preferred_;
    input.muted = v.value("inputMuted", false);
    input.solo = v.value("inputSolo", false);
    if (v.contains("inputBalance") && v["inputBalance"].is_number())
    {
      input.balance = std::clamp(v["inputBalance"].get<double>(), -1.0, 1.0);
      input.balance_available = true;
    }
    if (v.contains("inputRoutedToMaster") && v["inputRoutedToMaster"].is_boolean())
    {
      input.routed_to_master = v["inputRoutedToMaster"].get<bool>();
      input.master_routing_available = true;
    }
    input.volume_mul = v.value("inputVolumeMul", 1.0);
    input.volume_db = normalized_obs_volume_db(v);
    if (v.value("monitoringModeAvailable", false))
    {
      const auto mode = parse_obs_audio_monitor_type(v.value("monitorType", ""));
      if (mode)
      {
        input.monitoring_mode = *mode;
        input.monitoring_mode_available = true;
      }
    }
    input.active_program = v.value("videoActive", false);
    input.meter_state = input.active_program ? "silent" : "inactive";
    if (const auto previous = inputs_.find(input.id); previous != inputs_.end())
    {
      input.last_meter_ns = previous->second.last_meter_ns;
      input.peak_dbfs = previous->second.peak_dbfs;
      input.rms_dbfs = previous->second.rms_dbfs;
      input.input_peak_dbfs = previous->second.input_peak_dbfs;
      if (input.active_program == previous->second.active_program)
        input.meter_state = previous->second.meter_state;
    }
    replacement_inputs.emplace(input.id, std::move(input));
  }
  const auto same_audio_metadata = [](const auto& left, const auto& right)
  {
    if (left.size() != right.size())
      return false;
    for (const auto& [id, input] : left)
    {
      const auto found = right.find(id);
      if (found == right.end() || input.name != found->second.name ||
          input.backend_uuid != found->second.backend_uuid || input.kind != found->second.kind ||
          input.muted != found->second.muted || input.solo != found->second.solo ||
          input.balance != found->second.balance ||
          input.balance_available != found->second.balance_available ||
          input.routed_to_master != found->second.routed_to_master ||
          input.master_routing_available != found->second.master_routing_available ||
          input.volume_mul != found->second.volume_mul ||
          input.volume_db != found->second.volume_db ||
          input.monitoring_mode != found->second.monitoring_mode ||
          input.monitoring_mode_available != found->second.monitoring_mode_available ||
          input.active_program != found->second.active_program)
        return false;
    }
    return true;
  };
  const bool audio_inputs_changed = !same_audio_metadata(inputs_, replacement_inputs);
  inputs_ = std::move(replacement_inputs);
  audio_master_.reset();
  if (audio_master_volume_available_)
  {
    const auto master = b.find("audioMaster");
    if (master != b.end() && master->is_object() && master->contains("masterVolumeDb") &&
        (*master)["masterVolumeDb"].is_number())
      audio_master_ = AudioMaster{(*master)["masterVolumeDb"].get<double>(),
                                  master->value("masterMuted", false)};
  }
  stream_ = b.value("stream", stream_);
  streaming_destinations_.clear();
  if (streaming_destinations_available_)
  {
    streaming_destinations_ = parse_streaming_destinations(
        b.value("streamingDestinations", json::array()), streaming_destination_individual_control_);
    apply_stream_configuration(stream_, streaming_destinations_);
  }
  record_ = b.value("record", record_);
  const auto record_active = record_.value("active", false);
  auto record_duration_ms = record_.value("durationMs", 0ULL);
  if (!record_active)
    record_duration_ms = 0;
  record_["durationMs"] = record_duration_ms;
  const auto record_timecode = record_.value("timecode", std::string{});
  record_["timecode"] = record_active && valid_recording_timecode(record_timecode)
                            ? record_timecode
                            : format_recording_timecode(record_duration_ms);
  if (!record_.contains("observedAtMonotonicNs") ||
      !record_["observedAtMonotonicNs"].is_number_integer())
    record_["observedAtMonotonicNs"] = monotonic_ns();
  fresh_ = true;
  obs_connected_ = true;
  gateway_state_ = "ready";
  const auto qualified_layers = [&](const auto& groups)
  {
    return std::ranges::all_of(groups,
                               [&](const auto& group)
                               {
                                 return std::ranges::all_of(
                                     group.second,
                                     [&](const SceneItem& item)
                                     {
                                       return std::ranges::any_of(
                                           production_catalogs_.canvas->canvases,
                                           [&](const auto& canvas)
                                           { return canvas.id == item.canvas; });
                                     });
                               });
  };
  // A native poll may advance during reconciliation. Never advertise canvas support for a
  // baseline containing placements whose membership was not observed.
  if (production_catalogs_.canvas && (!qualified_layers(items_) || !qualified_layers(group_items_)))
    production_catalogs_.canvas.reset();
  changed_locked({{"fullSnapshot", true}});
  if (audio_inputs_changed)
  {
    const auto value = audio_inputs();
    for (const auto& listener : audio_input_listeners_)
      listener(value);
  }
  return true;
}

void StateStore::apply_obs_event(std::string_view type, const json& d)
{
  std::lock_guard lock(mutex_);
  json changed;
  auto find_scene_by_name = [&](std::string_view name) -> Scene*
  {
    for (auto& [_, scene] : scenes_)
      if (scene.name == name)
        return &scene;
    return nullptr;
  };
  auto find_scene = [&]() -> Scene*
  {
    const auto backend_id = d.value("sceneUuid", "");
    if (!backend_id.empty())
      for (auto& [_, scene] : scenes_)
        if (scene.backend_id == backend_id)
          return &scene;
    return find_scene_by_name(d.value("sceneName", ""));
  };
  auto find_input_by_name = [&](std::string_view name) -> AudioInput*
  {
    for (auto& [_, input] : inputs_)
      if (input.name == name)
        return &input;
    return nullptr;
  };
  auto find_input = [&]() -> AudioInput*
  {
    const auto input_uuid = d.value("inputUuid", "");
    if (!input_uuid.empty())
    {
      for (auto& [_, input] : inputs_)
        if (input.backend_uuid == input_uuid)
          return &input;
      return nullptr;
    }
    return find_input_by_name(d.value("inputName", ""));
  };
  const auto read_scene_item = [&](const json& value, const Scene& scene, std::uint32_t order)
  {
    SceneItem item;
    item.id = value.value("sceneItemId", 0LL);
    item.stable_layer_id = value.value("stableLayerId", "");
    item.source_name = value.value("sourceName", "");
    item.source_id = required_resource_id(value.value("sourceUuid", ""));
    item.source_kind = value.value("inputKind", value.value("sourceType", "unknown"));
    item.generic_kind = value.value("genericKind", "");
    item.enabled = value.value("sceneItemEnabled", false);
    item.base = value.value("baseLayer", false);
    item.visibility_controllable = value.value("visibilityControllable", true);
    item.visibility_state_available = value.value("visibilityStateAvailable", true);
    item.thumbnail_available = value.value("thumbnailAvailable", true);
    item.thumbnail_unavailable_reason = value.value("thumbnailUnavailableReason", "");
    item.order = value.value("layerOrder", order);
    item.group = value.value("isGroup", false);
    item.nested_scene = value.value("sourceType", "") == "OBS_SOURCE_TYPE_SCENE";
    item.audio_capable = value.value("audioCapable", false);
    item.visual_capability = parse_visual_capability(value.value("visualCapability", "unknown"));
    item.color = value.contains("color") && value["color"].is_string()
                     ? value["color"].get<std::string>()
                     : "";
    item.container_name = scene.name;
    item.source_width = source_dimension(value, "sourceWidth");
    item.source_height = source_dimension(value, "sourceHeight");
    return item;
  };

  if (type == "StudioModeStateChanged")
  {
    studio_mode_ = d.value("studioModeEnabled", false);
    if (!studio_mode_)
    {
      preview_scene_id_.clear();
      video_["preview"] = {{"available", false},
                           {"trackEpoch", video_["preview"].value("trackEpoch", 0)},
                           {"ageMs", nullptr}};
      changed["sceneState.previewSceneId"] = nullptr;
      changed["video.preview"] = video_["preview"];
    }
    changed["obs.studioMode"] = studio_mode_;
  }
  else if (type == "CurrentProgramSceneChanged" || type == "CurrentPreviewSceneChanged")
  {
    const bool program = type == "CurrentProgramSceneChanged";
    auto* scene = find_scene();
    if (!scene)
    {
      fresh_ = false;
      changed["resyncRequired"] = true;
    }
    else
    {
      for (auto& [_, value] : scenes_)
        (program ? value.is_program : value.is_preview) = false;
      (program ? scene->is_program : scene->is_preview) = true;
      (program ? program_scene_id_ : preview_scene_id_) = scene->id;
      changed[program ? "sceneState.programSceneId" : "sceneState.previewSceneId"] = scene->id;
    }
  }
  else if (type == "CurrentSceneTransitionChanged")
  {
    current_transition_name_ = d.value("transitionName", current_transition_name_);
    const auto transition_uuid = d.value("transitionUuid", "");
    auto found = std::find_if(transitions_.begin(),
                              transitions_.end(),
                              [&](const auto& entry)
                              {
                                return transition_uuid.empty()
                                           ? entry.second.name == current_transition_name_
                                           : entry.second.backend_id == transition_uuid;
                              });
    if (found == transitions_.end())
    {
      fresh_ = false;
      changed["resyncRequired"] = true;
    }
    else
    {
      current_transition_id_ = found->first;
      current_transition_kind_ = found->second.kind;
      transition_duration_ms_ = found->second.duration_ms;
      current_transition_fixed_ = found->second.fixed_duration;
      current_transition_configurable_ = found->second.configurable;
      changed["transition.currentTransitionId"] = current_transition_id_;
    }
  }
  else if (type == "CurrentSceneTransitionDurationChanged")
  {
    transition_duration_ms_ = std::clamp(d.value("transitionDuration", transition_duration_ms_),
                                         minimum_transition_duration_ms,
                                         maximum_transition_duration_ms);
    if (const auto current = transitions_.find(current_transition_id_);
        current != transitions_.end())
      current->second.duration_ms = transition_duration_ms_;
    changed["transition.durationMs"] = transition_duration_ms_;
  }
  else if (type == "CompositionLayersChanged")
  {
    ++source_observation_generation_;
    if (backend_id_ == "obs")
      changed["sourceMembership"] = true;
    auto* scene = find_scene();
    const auto values = d.find("sceneItems");
    if (!scene || values == d.end() || !values->is_array())
    {
      fresh_ = false;
      changed["resyncRequired"] = true;
    }
    else
    {
      const auto name = d.value("sceneName", scene->name);
      const bool metadata_changed = !name.empty() && name != scene->name;
      if (metadata_changed)
        scene->name = name;
      std::vector<SceneItem> replacement;
      replacement.reserve(values->size());
      for (const auto& value : *values)
        replacement.push_back(
            read_scene_item(value, *scene, static_cast<std::uint32_t>(replacement.size())));
      items_.insert_or_assign(scene->id, std::move(replacement));
      if (backend_id_ == "obs")
        source_membership_.replace(scene->id, read_source_occurrences(*values, scene->backend_id));
      ++scene->item_revision;
      ++source_catalog_generation_;
      changed["compositionLayers." + scene->id] = true;
      if (metadata_changed)
        changed["compositionMetadata." + scene->id] = true;
    }
  }
  else if (type == "SceneItemEnableStateChanged")
  {
    changed = apply_scene_visibility_locked(d);
  }
  else if (type == "MasterVolumeChanged")
  {
    if (audio_master_volume_available_ && d.contains("masterVolumeDb") &&
        d["masterVolumeDb"].is_number())
    {
      audio_master_ = AudioMaster{d["masterVolumeDb"].get<double>(), d.value("masterMuted", false)};
      if (audio_master_mute_available_)
        changed["audioMaster.muted"] = audio_master_->muted;
      changed["audioMaster.volumeDb"] = audio_master_->volume_db;
    }
  }
  else if (type == "InputMuteStateChanged" || type == "InputSoloStateChanged" ||
           type == "InputBalanceChanged" || type == "InputVolumeChanged" ||
           type == "InputMasterRoutingChanged" || type == "InputActiveStateChanged" ||
           type == "InputAudioMonitorTypeChanged")
  {
    if (auto* input = find_input())
    {
      if (type == "InputMuteStateChanged")
        input->muted = d.value("inputMuted", false);
      if (type == "InputSoloStateChanged")
        input->solo = d.value("inputSolo", false);
      if (type == "InputBalanceChanged")
      {
        input->balance = std::clamp(d.value("inputBalance", 0.0), -1.0, 1.0);
        input->balance_available = true;
      }
      if (type == "InputMasterRoutingChanged")
      {
        const auto routed = d.find("inputRoutedToMaster");
        if (!audio_input_master_routing_available_ || routed == d.end() || !routed->is_boolean())
        {
          fresh_ = false;
          changed["resyncRequired"] = true;
        }
        else
        {
          input->routed_to_master = routed->get<bool>();
          input->master_routing_available = true;
          if (const auto active = d.find("videoActive"); active != d.end() && active->is_boolean())
          {
            input->active_program = active->get<bool>();
            input->meter_state = input->active_program ? "silent" : "inactive";
          }
        }
      }
      if (type == "InputVolumeChanged")
      {
        input->volume_mul = d.value("inputVolumeMul", 1.0);
        input->volume_db = normalized_obs_volume_db(d);
      }
      if (type == "InputActiveStateChanged")
      {
        input->active_program = d.value("videoActive", false);
        input->meter_state = input->active_program ? "silent" : "inactive";
      }
      if (type == "InputAudioMonitorTypeChanged")
      {
        const auto mode = parse_obs_audio_monitor_type(d.value("monitorType", ""));
        if (!mode)
        {
          fresh_ = false;
          changed["resyncRequired"] = true;
        }
        else
        {
          input->monitoring_mode = *mode;
          input->monitoring_mode_available = true;
        }
      }
      if (type == "InputMasterRoutingChanged" && !changed.contains("resyncRequired"))
        changed["audioInputs." + input->id + ".routedToMaster"] = input->routed_to_master;
      else
      {
        json input_change = {{"muted", input->muted},
                             {"volumeMul", input->volume_mul},
                             {"volumeDb", input->volume_db},
                             {"monitoringMode", audio_monitoring_mode_name(input->monitoring_mode)},
                             {"activeInProgram", input->active_program}};
        if (audio_solo_available_)
          input_change["solo"] = input->solo;
        if (audio_input_balance_available_ && input->balance_available)
          input_change["balance"] = input->balance;
        if (audio_input_master_routing_available_ && input->master_routing_available)
          input_change["routedToMaster"] = input->routed_to_master;
        changed["audioInputs." + input->id] = std::move(input_change);
      }
    }
  }
  else if (type == "InputVolumeMeters")
  {
    if (!audio_meters_available_)
      return;
    for (const auto& meter : d.value("inputs", json::array()))
    {
      AudioInput* input = nullptr;
      const auto input_uuid = meter.value("inputUuid", "");
      if (!input_uuid.empty())
      {
        for (auto& [_, candidate] : inputs_)
          if (candidate.backend_uuid == input_uuid)
          {
            input = &candidate;
            break;
          }
      }
      if (!input)
        input = find_input_by_name(meter.value("inputName", ""));
      if (!input)
        continue;
      input->last_meter_ns = monotonic_ns();
      input->meter_state = input->active_program ? "live" : "inactive";
      input->peak_dbfs.clear();
      input->rms_dbfs.clear();
      input->input_peak_dbfs.clear();
      for (const auto& level : meter.value("inputLevelsMul", json::array()))
      {
        auto db = [](double mul)
        {
          return static_cast<float>(mul <= 0 ? -100.0 : std::max(-100.0, 20.0 * std::log10(mul)));
        };
        if (level.size() >= 3)
        {
          input->rms_dbfs.push_back(db(level[0]));
          input->peak_dbfs.push_back(db(level[1]));
          input->input_peak_dbfs.push_back(db(level[2]));
        }
      }
    }
    return; // High-rate samples do not increment reliable state revisions.
  }
  else if (type == "StreamStateChanged")
  {
    stream_["state"] = d.value("outputState", "unknown");
    stream_["active"] = d.value("outputActive", false);
    // A single aggregate-only row represents the output itself. Individual targets need their
    // own observations; a global event cannot identify which targets are transmitting.
    if (!streaming_multistream_available_ && streaming_destinations_.size() == 1)
    {
      auto& destination = streaming_destinations_.begin()->second;
      if (destination.enabled && destination.control_scope == "aggregate-only")
      {
        destination.state = streaming_output_state(stream_);
        if (destination.state != "error")
          destination.failure_reason.clear();
      }
    }
    changed["outputs.stream"] = stream_;
    if (streaming_destinations_available_ && !streaming_destinations_.empty())
      changed["streamingDestinations"] = true;
  }
  else if (type == "StreamingDestinationsChanged")
  {
    try
    {
      streaming_destinations_ =
          parse_streaming_destinations(d.value("streamingDestinations", json::array()),
                                       streaming_destination_individual_control_);
      if (d.contains("configurationKnown"))
        stream_["configurationKnown"] = d.at("configurationKnown").get<bool>();
      apply_stream_configuration(stream_, streaming_destinations_);
      changed["outputs.stream"] = stream_;
      changed["streamingDestinations"] = true;
    }
    catch (const std::exception&)
    {
      fresh_ = false;
      changed["resyncRequired"] = true;
    }
  }
  else if (type == "RecordStateChanged")
  {
    const auto observed_at = monotonic_ns();
    auto duration_ms = record_.value("durationMs", 0ULL);
    if (record_.value("state", "stopped") == "recording")
    {
      const auto previous_observation = record_.value("observedAtMonotonicNs", observed_at);
      if (observed_at > previous_observation)
        duration_ms += (observed_at - previous_observation) / 1'000'000;
    }
    const auto output_state = d.value("outputState", "");
    const auto normalized = normalize_recording_state(
        d.value("outputActive", false), output_state.find("PAUSED") != std::string::npos);
    if (!normalized.active)
      duration_ms = 0;
    record_ = {{"active", normalized.active},
               {"paused", normalized.paused},
               {"state", normalized.state},
               {"timecode", format_recording_timecode(duration_ms)},
               {"durationMs", duration_ms},
               {"observedAtMonotonicNs", observed_at}};
    changed["outputs.record"] = record_;
  }
  else if (type == "SceneListChanged" || type == "SceneCreated" || type == "SceneRemoved" ||
           type == "SceneNameChanged" || type == "SceneItemCreated" || type == "SceneItemRemoved" ||
           type == "SceneItemListReindexed" || type == "InputCreated" || type == "InputRemoved" ||
           type == "InputNameChanged" || type == "CurrentSceneCollectionChanging" ||
           type == "CurrentSceneCollectionChanged")
  {
    ++source_observation_generation_;
    fresh_ = false;
    changed["resyncRequired"] = true;
  }
  else
    return;
  changed_locked(std::move(changed));
}

void StateStore::set_video_health(std::string_view track, const json& health)
{
  std::lock_guard lock(mutex_);
  auto& current = video_[std::string(track)];
  const auto material = [](const json& value)
  {
    json result = json::object();
    for (auto key : {"available", "trackEpoch", "width", "height", "backend"})
      if (value.contains(key))
        result[key] = value[key];
    return result;
  };
  const bool changed = material(current) != material(health);
  current = health;
  // Frame timestamps and age are telemetry, not canonical mutations. Publishing
  // them as revisions made optimistic commands stale several times per second.
  if (changed)
    changed_locked({{"video." + std::string(track), health}});
}

void StateStore::set_performance(ProductionPerformanceSample sample)
{
  std::lock_guard lock(mutex_);
  performance_ = std::move(sample);
  performance_observed_at_ns_ = monotonic_ns();
  ++performance_sequence_;
}

std::optional<json> StateStore::generic_performance() const
{
  std::lock_guard lock(mutex_);
  if (!performance_supported_ || !obs_connected_ || !performance_)
    return std::nullopt;

  const auto add = [](json& target, std::string_view name, const auto& value)
  {
    if (value)
      target[std::string(name)] = *value;
  };
  json system = json::object();
  add(system, "cpuUsagePercent", performance_->cpu_usage_percent);
  add(system, "memoryUsageMiB", performance_->memory_usage_mib);
  add(system, "availableDiskSpaceMiB", performance_->available_disk_space_mib);
  json render = json::object();
  add(render, "framesPerSecond", performance_->frames_per_second);
  add(render, "averageFrameTimeMs", performance_->average_frame_time_ms);
  add(render, "skippedFrames", performance_->render_skipped_frames);
  add(render, "totalFrames", performance_->render_total_frames);
  json encode = json::object();
  add(encode, "skippedFrames", performance_->encode_skipped_frames);
  add(encode, "totalFrames", performance_->encode_total_frames);
  json stream = json::object();
  add(stream, "bitrateKbps", performance_->stream_bitrate_kbps);
  add(stream, "congestionRatio", performance_->stream_congestion_ratio);
  add(stream, "droppedFrames", performance_->stream_dropped_frames);
  add(stream, "droppedFramesPercent", performance_->stream_dropped_frames_percent);
  add(stream, "bytes", performance_->stream_bytes);
  add(stream, "durationMs", performance_->stream_duration_ms);

  const auto age_ns = monotonic_ns() - performance_observed_at_ns_;
  return json{{"type", "performance"},
              {"protocolVersion", 1},
              {"contractRevision", 2},
              {"productionProvider", backend_id_},
              {"sampleSequence", performance_sequence_},
              {"ageMs", age_ns / 1'000'000ULL},
              {"system", std::move(system)},
              {"render", std::move(render)},
              {"encode", std::move(encode)},
              {"stream", std::move(stream)}};
}

json StateStore::snapshot_locked() const
{
  return {{"type", "stateSnapshot"},
          {"revision", revision_},
          {"observedAtMonotonicNs", monotonic_ns()},
          {"gateway",
           {{"instanceId", instance_id_},
            {"name", name_},
            {"apiVersion", 1},
            {"state", gateway_state_}}},
          {"obs",
           {{"connected", obs_connected_},
            {"version", obs_version_},
            {"studioMode", studio_mode_},
            {"fresh", fresh_}}},
          {"outputs", {{"stream", stream_}, {"record", record_}}},
          {"video", video_},
          {"sceneState",
           {{"programSceneId", program_scene_id_},
            {"previewSceneId", studio_mode_ ? json(preview_scene_id_) : json(nullptr)}}}};
}
json StateStore::snapshot() const
{
  std::lock_guard lock(mutex_);
  return snapshot_locked();
}

json StateStore::generic_state(Role role) const
{
  std::lock_guard lock(mutex_);
  const bool usable = obs_connected_ && fresh_;
  const auto connection = usable            ? "usable"
                          : obs_connected_  ? "degraded"
                          : revision_ == 0U ? "connecting"
                                            : "disconnected";
  const auto health = usable ? "operational" : obs_connected_ ? "degraded" : "unavailable";
  json buses = json::object();
  if (!program_scene_id_.empty())
    buses["program"] = {{"compositionId", program_scene_id_}};
  if (studio_mode_ && !preview_scene_id_.empty())
    buses["preview"] = {{"compositionId", preview_scene_id_}};

  const bool record_active = record_.value("active", false);
  const bool record_paused = record_.value("paused", false);
  auto recording_state = record_.value("state", "stopped");
  if (recording_state != "stopped" && recording_state != "starting" &&
      recording_state != "recording" && recording_state != "paused" &&
      recording_state != "stopping" && recording_state != "error")
    recording_state = !record_active ? "stopped" : record_paused ? "paused" : "recording";
  const auto streaming_state = streaming_output_state(stream_);
  const bool stream_configuration_known = stream_.value("configurationKnown", true);
  const bool stream_configured = !stream_configuration_known || stream_.value("configured", false);

  json public_video = json::object();
  for (const auto* track : {"program", "preview"})
  {
    json value = json::object();
    for (const auto* field :
         {"available", "trackEpoch", "ageMs", "width", "height", "effectiveFps"})
      if (video_[track].contains(field))
        value[field] = video_[track][field];
    public_video[track] = std::move(value);
  }

  return {{"contractRevision", 2},
          {"stateSequence", revision_},
          {"connection", connection},
          {"system", {{"name", system_name_}, {"version", obs_version_}, {"health", health}}},
          {"buses", std::move(buses)},
          {"transition",
           {{"currentTransitionId", current_transition_id_},
            {"durationMs", transition_duration_ms_},
            {"fixedDuration", current_transition_fixed_}}},
          {"recordingOutputs",
           json::array({{{"outputId", "primary-recording"},
                         {"state", recording_state},
                         {"durationMs", record_.value("durationMs", 0ULL)}}})},
          {"streamingOutputs",
           json::array({{{"outputId", "primary-stream"},
                         {"state", streaming_state},
                         {"configured", stream_configured},
                         {"configurationKnown", stream_configuration_known}}})},
          {"access", {{"level", role_name(role)}}},
          {"video", std::move(public_video)},
          {"media",
           {{"programAvailable", video_["program"].value("available", false)},
            {"previewAvailable", studio_mode_ && video_["preview"].value("available", false)}}}};
}

json StateStore::generic_capabilities(Role role,
                                      std::uint64_t capability_revision,
                                      bool thumbnails_available,
                                      AudioMonitorStatus audio_monitor,
                                      AudioMonitorStatus audio_master) const
{
  std::lock_guard lock(mutex_);
  const bool backend_available = obs_connected_ && fresh_;
  const std::string backend_reason = obs_connected_ ? "resource-stale" : "disconnected";
  const bool can_control = role >= Role::operator_;
  const bool preview_available = backend_available && preview_available_ && studio_mode_;
  const bool stream_configured = stream_.value("configured", false);
  const json video_limits = {{"type", "video"},
                             {"supportedFps", json::array({1, 2, 5, 10, 15})},
                             {"maxWidth", 1920},
                             {"maxHeight", 1080}};
  const json thumbnail_limits = {
      {"type", "thumbnail"}, {"maxWidth", 320}, {"maxHeight", 320}, {"supportsEtag", true}};
  json features = json::object();
  const auto observe = [&](std::string_view name, bool available, std::string reason = {})
  { features[std::string(name)] = capability("observe", available, std::move(reason)); };
  const auto control = [&](std::string_view name, bool available, std::string reason = {})
  {
    if (can_control)
      features[std::string(name)] = capability("control", available, std::move(reason));
  };

  observe("system.health.observe", true);
  if (performance_supported_)
    features["system.performance.observe"] =
        capability("observe",
                   backend_available,
                   backend_reason,
                   {{"transport", "webrtc-data-channel"},
                    {"messageType", "performance"},
                    {"protocolVersion", 1},
                    {"bootstrapEndpoint", "/v1/performance"},
                    {"updateIntervalMs", performance_update_interval_ms},
                    {"staleAfterMs", performance_stale_after_ms},
                    {"metrics", performance_metrics_}});
  observe("composition.catalog.observe", backend_available, backend_reason);
  observe("composition.program.observe", backend_available, backend_reason);
  control("composition.program.select",
          backend_available && program_selection_available_,
          !backend_available ? backend_reason : "mode-inactive");
  if (preview_available_)
  {
    observe("composition.preview.observe",
            preview_available,
            preview_available ? ""
            : studio_mode_    ? backend_reason
                              : "mode-inactive");
    control("composition.preview.select",
            preview_available && preview_selection_available_,
            preview_available && preview_selection_available_ ? ""
            : studio_mode_                                    ? backend_reason
                                                              : "mode-inactive");
  }
  if (composition_monitor_render_available_)
    observe("composition.monitor.render", backend_available, backend_reason);
  observe("composition.layers.observe", backend_available, backend_reason);
  control("composition.layer.visibility",
          backend_available && layer_visibility_available_,
          !backend_available ? backend_reason : "backend-unavailable");
  observe("composition.layer.color.observe",
          backend_available && layer_colors_available_,
          !backend_available ? backend_reason : "backend-unavailable");
  if (transition_catalog_available_)
    observe("transition.catalog.observe", backend_available, backend_reason);
  if (can_control && transition_catalog_available_ && transition_execute_available_)
    features["transition.default"] = capability(
        default_transition_configuration_available_ && preview_available ? "control" : "observe",
        backend_available,
        backend_reason,
        {{"type", "default-transition"},
         {"mode", default_transition_uses_quick_play_ ? "quick-play" : "current-transition"},
         {"stateAvailable", default_transition_state_available_},
         {"configurationAvailable", default_transition_configuration_available_}});
  if (quick_transitions_available_)
    features["transition.quick"] = capability(can_control ? "control" : "observe",
                                              backend_available,
                                              backend_reason,
                                              {{"type", "quick-transition"},
                                               {"durationMs",
                                                {{"minimum", minimum_transition_duration_ms},
                                                 {"maximum", maximum_transition_duration_ms}}}});
  if (can_control && transition_configure_available_)
    features["transition.configure"] =
        capability("control",
                   backend_available,
                   backend_reason,
                   {{"type", "transition"},
                    {"durationMs",
                     {{"minimum", minimum_transition_duration_ms},
                      {"maximum", maximum_transition_duration_ms}}}});
  if (can_control && transition_execute_available_)
    features["transition.execute"] =
        capability("control",
                   preview_available,
                   preview_available ? ""
                   : studio_mode_    ? backend_reason
                                     : "mode-inactive",
                   {{"type", "transition"},
                    {"selection", "transitionId"},
                    {"legacyKinds", json::array({"cut-or-configured"})}});
  const bool program_video_available =
      backend_available && video_["program"].value("available", false);
  if (program_video_supported_)
    features["media.program.video"] =
        capability("observe",
                   program_video_available,
                   !backend_available ? backend_reason : "media-negotiating",
                   video_limits);
  const bool preview_video_available =
      preview_available && video_["preview"].value("available", false);
  if (preview_video_supported_)
    features["media.preview.video"] = capability("observe",
                                                 preview_video_available,
                                                 preview_video_available ? ""
                                                 : !studio_mode_         ? "mode-inactive"
                                                 : !backend_available    ? backend_reason
                                                                         : "media-negotiating",
                                                 video_limits);
  if (thumbnails_supported_)
  {
    features["media.composition.thumbnail"] =
        capability("observe",
                   backend_available && thumbnails_available,
                   !backend_available ? backend_reason : "backend-unavailable",
                   thumbnail_limits);
    features["media.layer.thumbnail"] =
        capability("observe",
                   backend_available && thumbnails_available,
                   !backend_available ? backend_reason : "backend-unavailable",
                   thumbnail_limits);
  }
  observe("audio.catalog.observe", backend_available, backend_reason);
  if (audio_monitor.supported)
  {
    const bool monitor_available = backend_available && audio_monitor.available;
    const auto reason = !backend_available             ? "disconnected"
                        : audio_monitor.reason.empty() ? "backend-unavailable"
                                                       : audio_monitor.reason;
    observe("audio.monitor.mix", monitor_available, monitor_available ? "" : reason);
  }
  if (audio_master.supported)
  {
    const bool master_available = backend_available && audio_master.available;
    const auto reason = !backend_available            ? "disconnected"
                        : audio_master.reason.empty() ? "backend-unavailable"
                                                      : audio_master.reason;
    observe("audio.master.mix", master_available, master_available ? "" : reason);
  }
  if (audio_meters_available_)
    observe("audio.meters.observe", backend_available, backend_reason);
  features["audio.monitor.audition-bus"] =
      capability(role >= Role::admin ? "control" : "observe",
                 backend_available && audio_source_audition_bus_available_,
                 !backend_available ? backend_reason : "backend-unavailable");
  if (audio_mute_available_)
    control("audio.input.mute", backend_available, backend_reason);
  if (audio_mute_available_ || audio_master_mute_available_)
    control("audio.track.mute", backend_available, backend_reason);
  if (audio_solo_available_)
    features["audio.input.solo"] =
        capability(can_control ? "control" : "observe", backend_available, backend_reason);
  if (audio_input_balance_available_)
  {
    const bool balance_available =
        backend_available &&
        std::all_of(inputs_.begin(),
                    inputs_.end(),
                    [](const auto& entry) { return entry.second.balance_available; });
    features["audio.input.balance"] =
        capability(can_control ? "control" : "observe",
                   balance_available,
                   balance_available   ? ""
                   : backend_available ? "backend-unavailable"
                                       : backend_reason,
                   {{"balance", {{"minimum", -1.0}, {"maximum", 1.0}, {"center", 0.0}}}});
  }
  if (audio_input_master_routing_available_)
  {
    const bool routing_available =
        backend_available &&
        std::all_of(inputs_.begin(),
                    inputs_.end(),
                    [](const auto& entry) { return entry.second.master_routing_available; });
    features["audio.input.master-routing"] = capability("observe",
                                                        routing_available,
                                                        routing_available   ? ""
                                                        : backend_available ? "backend-unavailable"
                                                                            : backend_reason);
  }
  if (audio_volume_available_)
  {
    json volume_db_limits = {{"minimum", audio_volume_minimum_db_},
                             {"maximum", audio_volume_maximum_db_}};
    if (!audio_volume_fader_curve_.empty())
      volume_db_limits["faderCurve"] = audio_volume_fader_curve_;
    features["audio.input.volume"] = capability(can_control ? "control" : "observe",
                                                backend_available,
                                                backend_reason,
                                                {{"volumeDb", std::move(volume_db_limits)}});
  }
  if (audio_input_positive_gain_available_ && audio_volume_maximum_db_ > 0.0)
    features["audio.input.positive-gain"] =
        capability("observe",
                   backend_available,
                   backend_reason,
                   {{"volumeDb", {{"minimum", 0.0}, {"maximum", audio_volume_maximum_db_}}}});
  if (audio_master_volume_available_)
    features["audio.master.volume"] = capability(
        can_control ? "control" : "observe",
        backend_available && audio_master_.has_value(),
        !backend_available          ? backend_reason
        : audio_master_.has_value() ? ""
                                    : "backend-unavailable",
        {{"volumeDb",
          {{"minimum", audio_volume_minimum_db_}, {"maximum", audio_volume_maximum_db_}}}});
  if (audio_monitoring_backend_available_)
  {
    features["audio.input.monitoring"] =
        capability(can_control ? "control" : "observe", backend_available, backend_reason);
  }
  if (recording_observe_available_)
    observe("output.record.observe", backend_available, backend_reason);
  if (recording_start_stop_available_)
    control("output.record.start-stop", backend_available, backend_reason);
  if (recording_pause_supported_ && can_control)
  {
    const bool pause_available = backend_available && recording_pause_available_;
    features["output.record.pause-resume"] =
        capability("control",
                   pause_available,
                   !backend_available ? backend_reason : "recording-pause-disabled");
  }
  if (streaming_observe_available_)
    observe("output.stream.observe", backend_available, backend_reason);
  if (streaming_start_stop_available_)
    control("output.stream.start-stop",
            backend_available &&
                (!stream_.value("configurationKnown", stream_configuration_known_) ||
                 stream_configured),
            !backend_available ? backend_reason : "not-configured");
  if (streaming_destinations_available_)
  {
    json limits = {{"type", "stream-destination-roster"},
                   {"selection", "destinationId"},
                   {"aggregateControl", streaming_start_stop_available_},
                   {"individualControl", streaming_destination_individual_control_},
                   {"multistream", streaming_multistream_available_}};
    if (streaming_destination_maximum_ != 0)
      limits["maximumDestinations"] = streaming_destination_maximum_;
    features["output.stream.multistream"] =
        capability(can_control && streaming_destination_individual_control_ ? "control" : "observe",
                   backend_available,
                   backend_reason,
                   std::move(limits));
  }
  observe("access.level.observe", true);
  features["access.level.request"] = capability("control", true);
  features["client.identity.rename"] = capability("control", true);
  return {{"contractRevision", 2},
          {"capabilityRevision", capability_revision},
          {"features", std::move(features)}};
}

json StateStore::generic_transitions() const
{
  std::lock_guard lock(mutex_);
  std::vector<SceneTransition> ordered;
  ordered.reserve(transitions_.size());
  for (const auto& [id, transition] : transitions_)
  {
    static_cast<void>(id);
    ordered.push_back(transition);
  }
  std::sort(
      ordered.begin(),
      ordered.end(),
      [](const SceneTransition& left, const SceneTransition& right)
      { return left.order != right.order ? left.order < right.order : left.name < right.name; });

  json transitions = json::array();
  for (const auto& transition : ordered)
  {
    transitions.push_back({{"transitionId", transition.id},
                           {"name", transition.name},
                           {"kind", transition.kind},
                           {"order", transition.order},
                           {"durationMs", transition.duration_ms},
                           {"active", transition.id == current_transition_id_},
                           {"fixedDuration", transition.fixed_duration},
                           {"configurable", transition.configurable},
                           {"fadeToBlack", transition.fade_to_black},
                           {"prepared", true}});
  }

  const auto current_transition = transitions_.find(current_transition_id_);
  const auto default_transition_display_name = current_transition != transitions_.end()
                                                   ? current_transition->second.name
                                                   : current_transition_name_;
  json default_transition = {
      {"mode", default_transition_uses_quick_play_ ? "quick-play" : "current-transition"},
      {"displayName",
       default_transition_uses_quick_play_ ? "Quick Play" : default_transition_display_name},
      {"stateAvailable", default_transition_state_available_},
      {"configurationAvailable", default_transition_configuration_available_},
      {"configurationLocation",
       default_transition_configuration_available_ ? "companion-api" : "production-backend"},
      {"configurationEndpoint",
       default_transition_configuration_available_ ? json("/v1/transitions/default")
                                                   : json(nullptr)}};
  default_transition["transitionId"] =
      default_transition_state_available_ ? json(current_transition_id_) : json(nullptr);
  default_transition["durationMs"] =
      default_transition_state_available_ ? json(transition_duration_ms_) : json(nullptr);
  default_transition["unavailableReason"] =
      default_transition_state_available_ && default_transition_configuration_available_
          ? json(nullptr)
          : json("backend-api-unavailable");

  return {{"contractRevision", 2},
          {"stateSequence", revision_},
          {"currentTransitionId", current_transition_id_},
          {"durationMs", transition_duration_ms_},
          {"defaultTransition", std::move(default_transition)},
          {"transitions", std::move(transitions)}};
}

json StateStore::generic_compositions(bool thumbnails_available) const
{
  std::lock_guard lock(mutex_);
  json compositions = json::array();
  for (std::size_t index = 0; index < scene_order_.size(); ++index)
  {
    const auto& scene = scenes_.at(scene_order_[index]);
    json colors = json::array();
    const auto items = items_.find(scene.id);
    if (items != items_.end())
      for (const auto& item : items->second)
        if (item.enabled)
          if (const auto color = rgb_color(item.color))
            colors.push_back(*color);
    compositions.push_back(
        {{"id", scene.id},
         {"name", scene.name},
         {"order", index},
         {"isProgram", scene.is_program},
         {"isPreview", scene.is_preview},
         {"activeLayerColors", std::move(colors)},
         {"thumbnail",
          thumbnails_available
              ? json{{"available", true},
                     {"endpoint", "/v1/compositions/" + scene.id + "/thumbnail"},
                     {"cacheToken", "composition-" + std::to_string(scene.item_revision)}}
              : json{{"available", false}, {"reason", "backend-unavailable"}}}});
  }
  return {{"contractRevision", 2},
          {"stateSequence", revision_},
          {"compositions", std::move(compositions)}};
}

json StateStore::generic_layers(std::string_view composition_id, bool thumbnails_available) const
{
  std::lock_guard lock(mutex_);
  if (!scenes_.contains(std::string(composition_id)))
  {
    throw std::out_of_range("Composition not found");
  }
  json layers = json::array();
  const auto append = [&](const SceneItem& item)
  {
    auto value = generic_layer(item);
    value["order"] = layers.size();
    value["zIndex"] = layers.size();
    if (!thumbnails_available && !item.group)
    {
      value["thumbnail"] = {{"available", false}, {"reason", "backend-unavailable"}};
    }
    layers.push_back(std::move(value));
  };
  if (const auto root = items_.find(std::string(composition_id)); root != items_.end())
  {
    for (const auto& item : root->second)
    {
      append(item);
      // Expand a group at its stacking position, below the next root layer.
      if (!item.group)
      {
        continue;
      }
      const auto children =
          group_items_.find(std::string(composition_id) + ":" + std::to_string(item.id));
      if (children == group_items_.end())
      {
        continue;
      }
      for (const auto& child : children->second)
      {
        append(child);
      }
    }
  }
  return {{"contractRevision", 2},
          {"stateSequence", revision_},
          {"compositionId", composition_id},
          {"sourceMembership", source_membership_locked(composition_id)},
          {"layers", std::move(layers)}};
}

json StateStore::generic_audio_inputs() const
{
  std::lock_guard lock(mutex_);
  json values = json::array();
  for (const auto& [id, input] : inputs_)
  {
    static_cast<void>(id);
    json value = {{"inputId", input.id},
                  {"trackId", "input:" + input.id},
                  {"name", input.name},
                  {"muted", input.muted},
                  {"kind", input.kind},
                  {"audioCapable", true},
                  {"volumeDb", input.volume_db}};
    if (audio_solo_available_)
      value["solo"] = input.solo;
    if (audio_input_balance_available_ && input.balance_available)
      value["balance"] = input.balance;
    if (audio_input_master_routing_available_ && input.master_routing_available)
      value["routedToMaster"] = input.routed_to_master;
    if (input.monitoring_mode_available)
      value["monitoringMode"] = audio_monitoring_mode_name(input.monitoring_mode);
    values.push_back(std::move(value));
  }
  json result = {{"contractRevision", 2},
                 {"stateSequence", revision_},
                 {"auditionBusActivationEnabled", audio_source_audition_bus_enabled_},
                 {"audioInputs", std::move(values)}};
  if (audio_master_volume_available_ && audio_master_)
  {
    result["master"] = {{"volumeDb", audio_master_->volume_db}, {"trackId", "audio-master"}};
    if (audio_master_mute_available_)
      result["master"]["muted"] = audio_master_->muted;
  }
  return result;
}

bool StateStore::streaming_destinations_available() const
{
  std::lock_guard lock(mutex_);
  return streaming_destinations_available_;
}

json StateStore::generic_streaming_destinations() const
{
  std::lock_guard lock(mutex_);
  if (!streaming_destinations_available_)
    throw std::runtime_error("capability-not-supported");

  const auto ordered = ordered_destinations(streaming_destinations_);
  json destinations = json::array();
  for (const auto& destination : ordered)
    destinations.push_back(streaming_destination_json(destination));

  return {{"contractRevision", 2},
          {"stateSequence", revision_},
          {"aggregate", streaming_destination_aggregate(ordered, streaming_output_state(stream_))},
          {"destinations", std::move(destinations)}};
}

json StateStore::scene_list() const
{
  std::lock_guard lock(mutex_);
  json values = json::array();
  for (const auto& [_, s] : scenes_)
    values.push_back({{"id", s.id},
                      {"name", s.name},
                      {"isProgram", s.is_program},
                      {"isPreview", s.is_preview},
                      {"sceneItemRevision", s.item_revision},
                      {"thumbnail",
                       {{"available", true},
                        {"endpoint", "/v1/scenes/" + s.id + "/thumbnail"},
                        {"defaultMaxWidth", 192},
                        {"defaultMaxHeight", 108}}}});
  return {{"type", "sceneList"}, {"revision", revision_}, {"scenes", values}};
}
json StateStore::scene_items(std::string_view id) const
{
  std::lock_guard lock(mutex_);
  auto scene_it = scenes_.find(std::string(id));
  auto found = items_.find(std::string(id));
  if (scene_it == scenes_.end() || found == items_.end())
    throw std::out_of_range("Scene not found");
  json values = json::array();
  for (const auto& i : found->second)
  {
    auto resource = scene_item_resource(i);
    resource["childrenEndpoint"] =
        i.group
            ? json("/v1/scenes/" + std::string(id) + "/groups/" + std::to_string(i.id) + "/items")
            : json(nullptr);
    values.push_back(std::move(resource));
  }
  return {
      {"sceneId", id}, {"sceneItemRevision", scene_it->second.item_revision}, {"items", values}};
}
json StateStore::group_items(std::string_view scene_id, int64_t group_item_id) const
{
  std::lock_guard lock(mutex_);
  auto scene_it = scenes_.find(std::string(scene_id));
  auto found = group_items_.find(std::string(scene_id) + ":" + std::to_string(group_item_id));
  if (scene_it == scenes_.end() || found == group_items_.end())
    throw std::out_of_range("Group not found");
  json values = json::array();
  for (const auto& i : found->second)
    values.push_back(scene_item_resource(i));
  return {{"sceneId", scene_id},
          {"groupSceneItemId", group_item_id},
          {"sceneItemRevision", scene_it->second.item_revision},
          {"items", values}};
}
json StateStore::audio_inputs() const
{
  std::lock_guard lock(mutex_);
  json values = json::array();
  for (const auto& [_, i] : inputs_)
  {
    json value = {{"inputId", i.id},
                  {"name", i.name},
                  {"audioCapable", true},
                  {"muted", i.muted},
                  {"volumeMul", i.volume_mul},
                  {"volumeDb", i.volume_db},
                  {"activeInProgram", i.active_program},
                  {"meterState", i.meter_state}};
    if (audio_solo_available_)
      value["solo"] = i.solo;
    if (audio_input_master_routing_available_ && i.master_routing_available)
      value["routedToMaster"] = i.routed_to_master;
    values.push_back(std::move(value));
  }
  return {{"type", "audioInputList"}, {"revision", revision_}, {"inputs", values}};
}

std::optional<std::vector<json>> StateStore::deltas_after(uint64_t revision) const
{
  std::lock_guard lock(mutex_);
  if (revision > revision_)
    return std::nullopt;
  if (revision == revision_)
    return std::vector<json>{};
  if (revision_journal_.empty() || revision + 1 < revision_journal_.front().at("revision"))
    return std::nullopt;

  std::vector<json> result;
  for (const auto& delta : revision_journal_)
    if (delta.at("revision").get<uint64_t>() > revision)
      result.push_back(delta);
  return result;
}
std::optional<json> StateStore::meter_frame(bool mixer_open)
{
  std::lock_guard lock(mutex_);
  if (!audio_meters_available_ || !mixer_open)
    return std::nullopt;
  const auto now = monotonic_ns();
  json meters = json::array();
  for (auto& [_, i] : inputs_)
  {
    if (i.active_program && now - i.last_meter_ns > 500'000'000ULL)
      i.meter_state = i.last_meter_ns ? "silent" : "stale";
    const auto linear_max = [](const std::vector<float>& values)
    {
      if (values.empty())
        return 0.0;
      const auto db = *std::max_element(values.begin(), values.end());
      return std::clamp(std::pow(10.0, static_cast<double>(db) / 20.0), 0.0, 1.0);
    };
    meters.push_back({{"inputId", i.id},
                      {"magnitude", linear_max(i.rms_dbfs)},
                      {"peak", linear_max(i.peak_dbfs)},
                      {"inputPeak", linear_max(i.input_peak_dbfs)}});
  }
  return json{{"type", "audioMeters"}, {"sequence", ++meter_sequence_}, {"meters", meters}};
}

CommandStateSnapshot StateStore::command_state_snapshot() const
{
  std::lock_guard lock(mutex_);
  CommandStateSnapshot snapshot;
  snapshot.revision_ = revision_;
  snapshot.fresh_ = fresh_ && obs_connected_;
  snapshot.studio_mode_ = studio_mode_;
  snapshot.transition_duration_ms_ = transition_duration_ms_;
  snapshot.program_scene_id_ = program_scene_id_;
  snapshot.preview_scene_id_ = preview_scene_id_;
  snapshot.program_frame_ns_ = video_["program"].value("observedFrameNs", 0ULL);
  snapshot.transition_completion_requires_video_ = transition_completion_requires_video_;
  snapshot.stream_active_ = stream_.value("active", false);
  snapshot.stream_configured_ = stream_.value("configured", false);
  snapshot.streaming_destinations_ = streaming_destinations_;
  snapshot.record_active_ = record_.value("active", false);
  snapshot.record_paused_ = record_.value("paused", false);
  snapshot.scenes_ = scenes_;
  snapshot.items_ = items_;
  snapshot.group_items_ = group_items_;
  snapshot.inputs_ = inputs_;
  snapshot.transitions_ = transitions_;
  snapshot.audio_master_ = audio_master_;
  return snapshot;
}

uint64_t StateStore::revision() const
{
  std::lock_guard lock(mutex_);
  return revision_;
}
bool StateStore::fresh() const
{
  std::lock_guard lock(mutex_);
  return fresh_ && obs_connected_;
}
bool StateStore::studio_mode() const
{
  std::lock_guard lock(mutex_);
  return studio_mode_;
}

std::uint32_t StateStore::transition_duration_ms() const
{
  std::lock_guard lock(mutex_);
  return transition_duration_ms_;
}

std::optional<std::string> StateStore::transition_name(std::string_view transition_id) const
{
  std::lock_guard lock(mutex_);
  auto found = transitions_.find(std::string(transition_id));
  return found == transitions_.end() ? std::nullopt : std::optional(found->second.name);
}

std::optional<SceneTransition> StateStore::transition(std::string_view transition_id) const
{
  std::lock_guard lock(mutex_);
  const auto found = transitions_.find(std::string(transition_id));
  return found == transitions_.end() ? std::nullopt : std::optional<SceneTransition>(found->second);
}

bool StateStore::current_transition(std::string_view transition_id) const
{
  std::lock_guard lock(mutex_);
  return current_transition_id_ == transition_id;
}

std::optional<Scene> StateStore::scene(std::string_view id) const
{
  std::lock_guard lock(mutex_);
  auto it = scenes_.find(std::string(id));
  return it == scenes_.end() ? std::nullopt : std::optional(it->second);
}
std::optional<SceneItem> StateStore::item(std::string_view id, int64_t item_id) const
{
  std::lock_guard lock(mutex_);
  auto it = items_.find(std::string(id));
  if (it != items_.end())
  {
    auto value = std::find_if(
        it->second.begin(), it->second.end(), [&](auto& i) { return i.id == item_id; });
    if (value != it->second.end())
      return *value;
  }
  const auto prefix = std::string(id) + ":";
  for (const auto& [key, children] : group_items_)
    if (key.starts_with(prefix))
    {
      auto value = std::find_if(
          children.begin(), children.end(), [&](const auto& i) { return i.id == item_id; });
      if (value != children.end())
        return *value;
    }
  return {};
}
std::optional<std::int64_t> StateStore::scene_item_id(std::string_view composition_id,
                                                      std::string_view layer_id) const
{
  const auto target = layer_command_target(composition_id, layer_id);
  return target ? std::optional<std::int64_t>(target->scene_item_id) : std::nullopt;
}

std::optional<LayerCommandTarget> StateStore::layer_command_target(std::string_view composition_id,
                                                                   std::string_view layer_id) const
{
  std::lock_guard lock(mutex_);
  const auto matches =
      [&](const std::vector<SceneItem>& values) -> std::optional<LayerCommandTarget>
  {
    const auto found =
        std::find_if(values.begin(),
                     values.end(),
                     [&](const SceneItem& item) { return generic_layer_id(item) == layer_id; });
    return found == values.end()
               ? std::nullopt
               : std::optional<LayerCommandTarget>({found->id,
                                                    found->source_id,
                                                    found->visibility_controllable,
                                                    found->visibility_state_available});
  };
  if (const auto root = items_.find(std::string(composition_id)); root != items_.end())
    if (const auto found = matches(root->second))
      return found;
  const auto prefix = std::string(composition_id) + ":";
  for (const auto& [key, children] : group_items_)
    if (key.starts_with(prefix))
      if (const auto found = matches(children))
        return found;
  return std::nullopt;
}
std::optional<SceneSource> StateStore::source(std::string_view source_id) const
{
  std::lock_guard lock(mutex_);
  std::optional<SceneSource> fallback;
  const auto find_source = [&](const auto& collections) -> std::optional<SceneSource>
  {
    for (const auto& [_, items] : collections)
      for (const auto& item : items)
        if (item.source_id == source_id)
        {
          SceneSource source{item.source_id,
                             item.source_name,
                             item.source_kind,
                             item.group,
                             item.nested_scene,
                             item.source_width,
                             item.source_height,
                             source_catalog_generation_};
          if (source.width != 0 && source.height != 0)
            return source;
          fallback = std::move(source);
        }
    return std::nullopt;
  };
  if (auto value = find_source(items_))
    return value;
  if (auto value = find_source(group_items_))
    return value;
  return fallback;
}
std::optional<AudioInput> StateStore::input(std::string_view id) const
{
  std::lock_guard lock(mutex_);
  auto it = inputs_.find(std::string(id));
  return it == inputs_.end() ? std::nullopt : std::optional(it->second);
}

std::size_t StateStore::solo_input_count() const noexcept
{
  std::lock_guard lock(mutex_);
  return static_cast<std::size_t>(std::count_if(
      inputs_.begin(), inputs_.end(), [](const auto& entry) { return entry.second.solo; }));
}
bool StateStore::audio_input_monitoring_available() const
{
  std::lock_guard lock(mutex_);
  return obs_connected_ && fresh_ &&
         std::all_of(inputs_.begin(),
                     inputs_.end(),
                     [](const auto& entry) { return entry.second.monitoring_mode_available; });
}

bool StateStore::audio_source_audition_bus_enabled() const
{
  std::lock_guard lock(mutex_);
  return audio_source_audition_bus_enabled_;
}
void StateStore::subscribe(Listener listener)
{
  std::lock_guard lock(mutex_);
  listeners_.push_back(std::move(listener));
}

void StateStore::subscribe_audio_inputs(AudioInputListener listener)
{
  std::lock_guard lock(mutex_);
  audio_input_listeners_.push_back(std::move(listener));
}
} // namespace studio_remote
