#include <studio_remote/streamlabs/streamlabs_adapter.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <studio_remote/streamlabs/streamlabs_direct_catalogs.hpp>
#include <studio_remote/streamlabs/streamlabs_protocol.hpp>

namespace studio_remote::streamlabs
{
namespace
{

using namespace std::chrono_literals;

std::int64_t stable_layer_id(std::string_view value)
{
  std::uint64_t hash = 1'469'598'103'934'665'603ULL;
  for (const unsigned char character : value)
  {
    hash ^= character;
    hash *= 1'099'511'628'211ULL;
  }
  hash &= 0x7fff'ffff'ffff'ffffULL;
  return static_cast<std::int64_t>(hash == 0 ? 1 : hash);
}

const Scene& selected_scene(const State& state, const json& data)
{
  const auto uuid = data.find("sceneUuid");
  if (uuid == data.end() || !uuid->is_string())
    throw std::invalid_argument("Streamlabs scene UUID is required");
  const auto* scene = find_scene(state, uuid->get_ref<const std::string&>());
  if (!scene)
    throw std::out_of_range("Streamlabs scene was not found");
  return *scene;
}

const AudioSource& selected_audio_source(const State& state, const json& data)
{
  const auto uuid = data.find("inputUuid");
  if (uuid == data.end() || !uuid->is_string())
    throw std::invalid_argument("Streamlabs audio source UUID is required");
  const auto* source = find_audio_source(state, uuid->get_ref<const std::string&>());
  if (!source)
    throw std::out_of_range("Streamlabs audio source was not found");
  return *source;
}

json scene_item(const State& state, const SceneNode& node)
{
  const bool folder = node.type == "folder";
  const bool audio_capable = find_audio_source(state, node.source_id) != nullptr;
  return {{"sceneItemId", stable_layer_id(node.id)},
          {"stableLayerId", node.id},
          {"canvas", node.canvas},
          {"visibilityControllable", canvas_allows_control(state.canvas, node.canvas)},
          {"sourceName", node.name},
          {"sourceUuid", folder ? node.id : node.source_id},
          {"inputKind", node.type},
          {"sourceType", folder ? "OBS_SOURCE_TYPE_SCENE" : "OBS_SOURCE_TYPE_INPUT"},
          {"isGroup", folder},
          {"sceneItemEnabled", node.visible},
          {"audioCapable", audio_capable},
          {"color", nullptr},
          {"visualCapability", "unknown"}};
}

json performance_response(const PerformanceState& performance)
{
  json result = json::object();
  const auto add = [&result](std::string_view name, const auto& value)
  {
    if (value)
      result[std::string(name)] = *value;
  };
  add("cpuUsagePercent", performance.cpu_usage_percent);
  add("framesPerSecond", performance.frame_rate);
  add("streamBitrateKbps", performance.bandwidth_kbps);
  add("streamDroppedFrames", performance.dropped_frames);
  add("streamDroppedFramesPercent", performance.dropped_frames_percent);
  return result;
}

} // namespace

StreamlabsAdapter::StreamlabsAdapter(std::unique_ptr<IStreamlabsTransport> transport)
    : transport_(std::move(transport))
{
  if (!transport_)
    throw std::invalid_argument("Streamlabs transport is required");
}

StreamlabsAdapter::~StreamlabsAdapter()
{
  stop();
}

void StreamlabsAdapter::reconfigure(std::unique_ptr<IStreamlabsTransport> transport)
{
  if (!transport)
    throw std::invalid_argument("Streamlabs transport is required");
  std::lock_guard command(command_mutex_);
  const bool was_running = thread_.joinable();
  stop();
  {
    std::lock_guard connection(transport_mutex_);
    transport_ = std::move(transport);
    next_program_identity_probe_ = {};
  }
  if (was_running)
    start();
}

void StreamlabsAdapter::start()
{
  std::lock_guard lock(mutex_);
  if (thread_.joinable())
    return;
  stopping_ = false;
  refresh_requested_ = true;
  thread_ = std::thread([this] { run(); });
}

void StreamlabsAdapter::stop()
{
  stopping_ = true;
  wake_.notify_all();
  transport_->disconnect();
  if (thread_.joinable())
    thread_.join();

  ConnectionCallback callback;
  bool notify = false;
  {
    std::lock_guard lock(mutex_);
    notify = connected_;
    connected_ = false;
    callback = connection_callback_;
  }
  if (notify && callback)
    callback(false, {});
}

bool StreamlabsAdapter::connected() const
{
  std::lock_guard lock(mutex_);
  return connected_;
}

void StreamlabsAdapter::on_event(EventCallback callback)
{
  std::lock_guard lock(mutex_);
  event_callback_ = std::move(callback);
}

void StreamlabsAdapter::on_connection(ConnectionCallback callback)
{
  std::lock_guard lock(mutex_);
  connection_callback_ = std::move(callback);
}

ProductionBackendInfo StreamlabsAdapter::backend_info() const
{
  ProductionBackendInfo info;
  info.id = "streamlabs";
  info.display_name = "Streamlabs Desktop";
  auto& capabilities = info.capabilities;
  {
    std::lock_guard lock(mutex_);
    capabilities.program_selection = !state_.studio_mode_enabled;
    capabilities.preview_selection = state_.studio_mode_enabled;
  }
  capabilities.preview = true;
  capabilities.composition_monitor_render = false;
  capabilities.native_audio_roster_required = false;
  capabilities.layer_visibility = true;
  capabilities.layer_colors = false;
  capabilities.transition_catalog = false;
  capabilities.transition_configure = false;
  capabilities.transition_execute = true;
  capabilities.program_video = false;
  capabilities.preview_video = false;
  capabilities.thumbnails = false;
  capabilities.audio_meters = false;
  capabilities.audio_monitoring = false;
  capabilities.audio_mute = true;
  capabilities.audio_volume = true;
  capabilities.audio_input_positive_gain = false;
  capabilities.audio_volume_minimum_db = -100.0;
  capabilities.audio_volume_maximum_db = 0.0;
  capabilities.audio_volume_fader_curve = "streamlabs-iec";
  capabilities.audio_solo = false;
  capabilities.audio_input_balance = false;
  capabilities.audio_input_master_routing = false;
  capabilities.audio_master_volume = false;
  capabilities.audio_source_audition_bus = false;
  capabilities.recording_pause = false;
  capabilities.recording_observe = true;
  capabilities.recording_start_stop = true;
  capabilities.streaming_observe = true;
  capabilities.streaming_start_stop = true;
  capabilities.streaming_destinations = true;
  capabilities.streaming_multistream = false;
  capabilities.streaming_destination_individual_control = false;
  capabilities.streaming_destination_maximum = 1;
  capabilities.stream_configuration_known = false;
  capabilities.quick_transitions = false;
  capabilities.default_transition_state_available = false;
  capabilities.default_transition_configuration_available = false;
  capabilities.transition_completion_requires_video = false;
  capabilities.stable_command_identifiers_preferred = true;
  capabilities.authoritative_control_verification = true;
  capabilities.explicit_start_stop_for_record = false;
  capabilities.explicit_start_stop_for_stream = false;
  {
    std::lock_guard lock(mutex_);
    capabilities.hotkey_trigger = state_.configured_actions.has_value();
    capabilities.program_observation = state_.program_identity_available;
    capabilities.system_hotkeys = false;
    capabilities.scene_collections = state_.collections.has_value();
    capabilities.streaming_destinations = state_.destinations.has_value();
    capabilities.stream_configuration_known = state_.destinations.has_value();
    capabilities.streaming_multistream = state_.destinations.has_value();
    // Saved roster size is not the native destination capacity. Omit an unknown ceiling.
    capabilities.streaming_destination_maximum = 0;
    capabilities.transition_execute = state_.program_identity_available;
  }
  capabilities.performance = true;
  capabilities.performance_metrics = {"system.cpuUsagePercent",
                                      "render.framesPerSecond",
                                      "stream.bitrateKbps",
                                      "stream.droppedFrames",
                                      "stream.droppedFramesPercent"};
  return info;
}

std::string StreamlabsAdapter::last_error() const
{
  std::lock_guard lock(mutex_);
  return last_error_;
}

void StreamlabsAdapter::request_refresh()
{
  {
    std::lock_guard lock(mutex_);
    refresh_requested_ = true;
  }
  wake_.notify_all();
}

void StreamlabsAdapter::run()
{
  auto retry_delay = 250ms;
  auto next_full_refresh = std::chrono::steady_clock::now();
  while (!stopping_)
  {
    try
    {
      bool observation_succeeded = false;
      if (!transport_->connected())
      {
        transport_->connect();
        transport_->subscribe([this](std::string_view resource, const json& data)
                              { observe_event(resource, data); });
        next_full_refresh = std::chrono::steady_clock::now();
      }

      bool requested = false;
      bool studio_mode = false;
      {
        std::lock_guard lock(mutex_);
        requested = refresh_requested_;
        refresh_requested_ = false;
        studio_mode = state_.studio_mode_enabled;
      }
      const auto now = std::chrono::steady_clock::now();
      if (requested || now >= next_full_refresh)
      {
        refresh();
        observation_succeeded = true;
        next_full_refresh = std::chrono::steady_clock::now() + 1s;
      }
      else if (studio_mode)
      {
        refresh_program_identity();
        observation_succeeded = true;
      }

      ConnectionCallback connection_callback;
      std::string version;
      bool notify_connected = false;
      {
        std::lock_guard lock(mutex_);
        notify_connected = !connected_;
        if (notify_connected)
          ++collection_revision_;
        connected_ = true;
        if (observation_succeeded)
        {
          consecutive_worker_failures_ = 0;
          last_error_.clear();
        }
        version = state_.desktop_version;
        connection_callback = connection_callback_;
      }
      if (notify_connected && connection_callback)
        connection_callback(true, version);
      retry_delay = 250ms;

      std::unique_lock lock(mutex_);
      const auto poll_delay = state_.studio_mode_enabled ? 100ms : 1000ms;
      wake_.wait_for(lock, poll_delay, [this] { return stopping_ || refresh_requested_; });
    }
    catch (const std::exception& error)
    {
      transport_->disconnect();
      ConnectionCallback connection_callback;
      bool notify_disconnected = false;
      bool recover_without_outage = false;
      {
        std::lock_guard lock(mutex_);
        ++consecutive_worker_failures_;
        recover_without_outage = connected_ && consecutive_worker_failures_ == 1;
        notify_disconnected = connected_ && !recover_without_outage;
        if (!recover_without_outage)
          connected_ = false;
        last_error_ = std::string(error.what()).substr(0, 240);
        connection_callback = connection_callback_;
      }
      if (notify_disconnected && connection_callback)
        connection_callback(false, {});
      if (stopping_)
        break;
      if (recover_without_outage)
        retry_delay = 250ms;
      const auto jitter = std::chrono::milliseconds(
          static_cast<int>(std::chrono::steady_clock::now().time_since_epoch().count() % 101));
      std::unique_lock lock(mutex_);
      wake_.wait_for(lock, retry_delay + jitter, [this] { return stopping_.load(); });
      retry_delay = std::min(retry_delay * 2, 8'000ms);
    }
  }
}

void StreamlabsAdapter::refresh(bool provider_observation)
{
  State replacement;
  std::size_t output_revision = 0;
  {
    std::lock_guard lock(mutex_);
    output_revision = output_event_revision_;
  }
  {
    std::lock_guard transport_lock(transport_mutex_);
    replacement = transport_->read_state();
  }
  publish(std::move(replacement), provider_observation, output_revision);
}

void StreamlabsAdapter::refresh_program_identity()
{
  State replacement;
  {
    std::lock_guard lock(mutex_);
    if (!state_.studio_mode_enabled)
      return;
    replacement = state_;
  }
  {
    std::lock_guard transport_lock(transport_mutex_);
    if (std::chrono::steady_clock::now() < next_program_identity_probe_)
      return;
    try
    {
      replacement.program_scene_id = transport_->read_program_scene_id();
      if (!find_scene(replacement, replacement.program_scene_id))
        throw std::runtime_error("Unknown Program scene");
      replacement.program_identity_available = true;
    }
    catch (const std::exception&)
    {
      replacement.program_scene_id.clear();
      replacement.program_identity_available = false;
      next_program_identity_probe_ = std::chrono::steady_clock::now() + 5s;
    }
  }
  EventCallback callback;
  bool availability_changed = false;
  bool program_changed = false;
  std::string program_name;
  {
    std::lock_guard lock(mutex_);
    // A narrow Program probe must not overwrite output events or a newer collection snapshot.
    if (state_.active_collection.id != replacement.active_collection.id ||
        !state_.studio_mode_enabled)
      return;
    availability_changed =
        state_.program_identity_available != replacement.program_identity_available;
    program_changed = state_.program_scene_id != replacement.program_scene_id;
    state_.program_identity_available = replacement.program_identity_available;
    state_.program_scene_id = replacement.program_scene_id;
    if (const auto* scene = find_scene(state_, state_.program_scene_id))
      program_name = scene->name;
    callback = event_callback_;
  }
  if (callback && availability_changed)
    callback({"SceneListChanged", json::object()});
  if (callback && program_changed)
    callback({"CurrentProgramSceneChanged",
              {{"sceneName", program_name}, {"sceneUuid", replacement.program_scene_id}}});
}

void StreamlabsAdapter::observe_event(std::string_view resource, const json& data)
{
  const bool recording = resource == "StreamingService.recordingStatusChange";
  const bool streaming = resource == "StreamingService.streamingStatusChange";
  if ((recording || streaming) && data.is_string())
  {
    const auto value = data.get<std::string>();
    const auto output = parse_output_state(value);
    if (output != OutputState::unknown)
    {
      State replacement;
      {
        std::lock_guard lock(mutex_);
        ++output_event_revision_;
        replacement = state_;
      }
      if (recording)
        replacement.recording = output;
      else
        replacement.streaming = output;
      const bool terminal = output != OutputState::starting && output != OutputState::stopping;
      publish(std::move(replacement), terminal);
      if (!terminal)
        return;
    }
  }
  request_refresh();
}

void StreamlabsAdapter::publish(State replacement,
                                bool provider_observation,
                                std::optional<std::size_t> expected_output_revision)
{
  EventCallback callback;
  State previous;
  {
    std::lock_guard lock(mutex_);
    if (expected_output_revision && *expected_output_revision != output_event_revision_)
    {
      replacement.recording = state_.recording;
      replacement.streaming = state_.streaming;
    }
    previous = state_;
    if (previous.collections != replacement.collections ||
        previous.active_collection != replacement.active_collection)
      ++collection_revision_;
    state_ = replacement;
    callback = event_callback_;
  }
  if (!callback || !provider_observation)
    return;

  if (previous.active_collection.id != replacement.active_collection.id ||
      previous.scenes != replacement.scenes || previous.canvas != replacement.canvas ||
      previous.destinations != replacement.destinations ||
      previous.configured_actions != replacement.configured_actions ||
      previous.program_identity_available != replacement.program_identity_available)
    callback({"SceneListChanged", json::object()});

  if (previous.studio_mode_enabled != replacement.studio_mode_enabled)
    callback({"StudioModeStateChanged", {{"studioModeEnabled", replacement.studio_mode_enabled}}});

  if (previous.program_scene_id != replacement.program_scene_id)
  {
    const auto* scene = find_scene(replacement, replacement.program_scene_id);
    callback(
        {"CurrentProgramSceneChanged",
         {{"sceneName", scene ? scene->name : ""}, {"sceneUuid", replacement.program_scene_id}}});
  }
  if (previous.preview_scene_id != replacement.preview_scene_id && replacement.studio_mode_enabled)
  {
    const auto* scene = find_scene(replacement, replacement.preview_scene_id);
    callback(
        {"CurrentPreviewSceneChanged",
         {{"sceneName", scene ? scene->name : ""}, {"sceneUuid", replacement.preview_scene_id}}});
  }

  for (const auto& scene : replacement.scenes)
  {
    const auto* old_scene = find_scene(previous, scene.id);
    for (const auto& node : scene.nodes)
    {
      const auto* old_node = old_scene ? find_scene_node(previous, scene.id, node.id) : nullptr;
      if (old_node && old_node->visible != node.visible)
        callback(
            {"SceneItemEnableStateChanged",
             {{"sceneName", scene.name},
              {"sceneUuid", scene.id},
              {"sceneItemId", stable_layer_id(node.id)},
              {"stableLayerId", node.id},
              {"canvas", node.canvas},
              {"visibilityControllable", canvas_allows_control(replacement.canvas, node.canvas)},
              {"sceneItemEnabled", node.visible}}});
    }
  }

  for (const auto& source : replacement.audio_sources)
  {
    const auto* old_source = find_audio_source(previous, source.source_id);
    if (old_source && old_source->muted != source.muted)
      callback({"InputMuteStateChanged",
                {{"inputName", source.name},
                 {"inputUuid", source.source_id},
                 {"inputMuted", source.muted}}});
    if (old_source && std::abs(old_source->volume_db - source.volume_db) > 0.001)
      callback({"InputVolumeChanged",
                {{"inputName", source.name},
                 {"inputUuid", source.source_id},
                 {"inputVolumeDb", source.volume_db},
                 {"inputVolumeMul", source.volume_multiplier}}});
  }

  if (previous.streaming != replacement.streaming)
    callback({"StreamStateChanged",
              {{"outputActive", output_state_active(replacement.streaming)},
               {"outputState", output_state_name(replacement.streaming)}}});
  if (previous.recording != replacement.recording)
    callback({"RecordStateChanged",
              {{"outputActive", output_state_active(replacement.recording)},
               {"outputPaused", false},
               {"outputState", output_state_name(replacement.recording)}}});
}

json StreamlabsAdapter::request(std::string_view type,
                                json data,
                                std::source_location caller,
                                std::string_view reason)
{
  static_cast<void>(caller);
  static_cast<void>(reason);
  State snapshot;
  {
    std::lock_guard lock(mutex_);
    if (!connected_)
      throw std::runtime_error("Streamlabs Desktop is not connected");
    snapshot = state_;
  }
  if (type.starts_with("Get"))
    return read_request(type, data, snapshot);

  std::lock_guard command_lock(command_mutex_);
  if (type == "SetSceneItemEnabled")
  {
    refresh();
    std::lock_guard lock(mutex_);
    snapshot = state_;
  }
  ControlObservation observation;
  {
    std::lock_guard transport_lock(transport_mutex_);
    observation = mutation(type, data, snapshot);
  }
  if (observation.kind == ControlKind::streaming)
  {
    request_refresh();
    return json::object();
  }
  settle_control(observation);
  return json::object();
}

json StreamlabsAdapter::request_current(std::string_view type,
                                        json data,
                                        std::source_location caller,
                                        std::string_view reason)
{
  static_cast<void>(caller);
  static_cast<void>(reason);
  if (!type.starts_with("Get"))
    throw std::invalid_argument("Streamlabs current-state request must be observational");

  if (type == "GetStudioModeEnabled")
  {
    std::lock_guard transport_lock(transport_mutex_);
    return {{"studioModeEnabled", transport_->read_studio_mode_enabled()}};
  }
  if (type == "GetPerformance")
  {
    PerformanceState performance;
    {
      std::lock_guard transport_lock(transport_mutex_);
      performance = transport_->read_performance();
    }
    {
      std::lock_guard lock(mutex_);
      state_.performance = performance;
    }
    return performance_response(performance);
  }
  if (type == "GetRecordStatus" || type == "GetStreamStatus")
  {
    std::pair<OutputState, OutputState> outputs;
    std::size_t output_revision = 0;
    {
      std::lock_guard lock(mutex_);
      output_revision = output_event_revision_;
    }
    {
      std::lock_guard transport_lock(transport_mutex_);
      outputs = transport_->read_output_states();
    }
    const auto [recording, streaming] = outputs;
    State replacement;
    {
      std::lock_guard lock(mutex_);
      replacement = state_;
    }
    replacement.recording = recording;
    replacement.streaming = streaming;
    publish(std::move(replacement), true, output_revision);
    OutputState effective_recording = OutputState::unknown;
    OutputState effective_streaming = OutputState::unknown;
    {
      std::lock_guard lock(mutex_);
      effective_recording = state_.recording;
      effective_streaming = state_.streaming;
    }
    if (type == "GetRecordStatus")
      return {{"outputActive", output_state_active(effective_recording)},
              {"outputPaused", false},
              {"outputDuration", 0},
              {"outputTimecode", "00:00:00.000"},
              {"outputState", output_state_name(effective_recording)}};
    return {{"outputActive", output_state_active(effective_streaming)},
            {"outputState", output_state_name(effective_streaming)}};
  }
  if (type == "GetCurrentProgramScene" || type == "GetCurrentPreviewScene")
  {
    State snapshot;
    {
      std::lock_guard lock(mutex_);
      snapshot = state_;
    }
    std::string scene_id;
    {
      std::lock_guard transport_lock(transport_mutex_);
      const bool studio_mode = transport_->read_studio_mode_enabled();
      if (type == "GetCurrentPreviewScene" && !studio_mode)
        throw std::runtime_error("Streamlabs Preview is unavailable outside Studio Mode");
      scene_id = type == "GetCurrentProgramScene" && studio_mode
                     ? transport_->read_program_scene_id()
                     : transport_->read_active_scene_id();
    }
    const auto* scene = find_scene(snapshot, scene_id);
    if (!scene)
      throw std::runtime_error("Streamlabs current scene is absent from the scene catalog");
    return type == "GetCurrentProgramScene" ? json{{"currentProgramSceneName", scene->name},
                                                   {"currentProgramSceneUuid", scene_id}}
                                            : json{{"currentPreviewSceneName", scene->name},
                                                   {"currentPreviewSceneUuid", scene_id}};
  }
  if (type == "GetSceneItemEnabled")
  {
    State replacement;
    {
      std::lock_guard lock(mutex_);
      replacement = state_;
    }
    const auto& scene = selected_scene(replacement, data);
    const auto item_id = data.at("sceneItemId").get<std::int64_t>();
    const auto node = std::find_if(scene.nodes.begin(),
                                   scene.nodes.end(),
                                   [&](const SceneNode& candidate)
                                   { return stable_layer_id(candidate.id) == item_id; });
    if (node == scene.nodes.end())
      throw std::out_of_range("Streamlabs scene item was not found");
    bool visible = false;
    {
      std::lock_guard transport_lock(transport_mutex_);
      visible = transport_->read_scene_item_visibility(node->resource_id);
    }
    SceneNode* updated = nullptr;
    for (auto& candidate_scene : replacement.scenes)
      for (auto& candidate_node : candidate_scene.nodes)
        if (candidate_scene.id == scene.id && candidate_node.id == node->id)
          updated = &candidate_node;
    if (!updated)
      throw std::runtime_error("Streamlabs scene item disappeared during observation");
    updated->visible = visible;
    publish(std::move(replacement), true);
    return {{"sceneItemEnabled", visible}};
  }
  if (type == "GetInputMute" || type == "GetInputVolume")
  {
    State replacement;
    {
      std::lock_guard lock(mutex_);
      replacement = state_;
    }
    const auto& cached = selected_audio_source(replacement, data);
    AudioSource observed;
    {
      std::lock_guard transport_lock(transport_mutex_);
      observed = transport_->read_audio_source(cached.resource_id);
    }
    const auto updated = std::find_if(replacement.audio_sources.begin(),
                                      replacement.audio_sources.end(),
                                      [&](const AudioSource& candidate)
                                      { return candidate.source_id == cached.source_id; });
    if (updated == replacement.audio_sources.end())
      throw std::runtime_error("Streamlabs audio source disappeared during observation");
    updated->muted = observed.muted;
    updated->volume_db = observed.volume_db;
    updated->volume_multiplier = observed.volume_multiplier;
    publish(std::move(replacement), true);
    if (type == "GetInputMute")
      return {{"inputMuted", observed.muted}};
    return {{"inputVolumeDb", observed.volume_db}, {"inputVolumeMul", observed.volume_multiplier}};
  }

  State replacement;
  {
    std::lock_guard transport_lock(transport_mutex_);
    replacement = transport_->read_state();
  }
  const auto result = read_request(type, data, replacement);
  publish(std::move(replacement), true);
  return result;
}

json StreamlabsAdapter::read_request(std::string_view type,
                                     const json& data,
                                     const State& state) const
{
  if (type == "GetVersion")
    return {{"obsVersion", state.desktop_version}};
  if (type == "GetPerformance")
    return performance_response(state.performance);
  if (type == "GetStudioModeEnabled")
    return {{"studioModeEnabled", state.studio_mode_enabled}};
  if (type == "GetSceneList")
  {
    json scenes = json::array();
    for (const auto& scene : state.scenes)
      scenes.push_back({{"sceneName", scene.name}, {"sceneUuid", scene.id}});
    const auto* program = find_scene(state, state.program_scene_id);
    const auto* preview = find_scene(state, state.preview_scene_id);
    return {{"currentProgramSceneName", program ? program->name : ""},
            {"currentProgramSceneUuid", state.program_scene_id},
            {"currentPreviewSceneName", preview ? preview->name : ""},
            {"currentPreviewSceneUuid", state.preview_scene_id},
            {"scenes", std::move(scenes)}};
  }
  if (type == "GetCurrentProgramScene")
  {
    const auto* program = find_scene(state, state.program_scene_id);
    return {{"currentProgramSceneName", program ? program->name : ""},
            {"currentProgramSceneUuid", state.program_scene_id}};
  }
  if (type == "GetCurrentPreviewScene")
  {
    if (!state.studio_mode_enabled)
      throw std::runtime_error("Streamlabs Preview is unavailable outside Studio Mode");
    const auto* preview = find_scene(state, state.preview_scene_id);
    return {{"currentPreviewSceneName", preview ? preview->name : ""},
            {"currentPreviewSceneUuid", state.preview_scene_id}};
  }
  if (type == "GetSceneItemList")
  {
    const auto& scene = selected_scene(state, data);
    json items = json::array();
    for (const auto& node : scene.nodes)
      if (node.parent_id.empty())
        items.push_back(scene_item(state, node));
    return {{"sceneItems", std::move(items)}};
  }
  if (type == "GetGroupSceneItemList")
  {
    const auto group = data.find("groupUuid");
    if (group == data.end() || !group->is_string())
      throw std::invalid_argument("Streamlabs group UUID is required");
    json items = json::array();
    for (const auto& scene : state.scenes)
      for (const auto& node : scene.nodes)
        if (node.parent_id == group->get_ref<const std::string&>())
          items.push_back(scene_item(state, node));
    return {{"sceneItems", std::move(items)}};
  }
  if (type == "GetSceneItemEnabled")
  {
    const auto& scene = selected_scene(state, data);
    const auto item_id = data.at("sceneItemId").get<std::int64_t>();
    const auto node = std::find_if(scene.nodes.begin(),
                                   scene.nodes.end(),
                                   [&](const SceneNode& candidate)
                                   { return stable_layer_id(candidate.id) == item_id; });
    if (node == scene.nodes.end())
      throw std::out_of_range("Streamlabs scene item was not found");
    return {{"sceneItemEnabled", node->visible}};
  }
  if (type == "GetInputList")
  {
    json inputs = json::array();
    for (const auto& source : state.audio_sources)
      inputs.push_back(
          {{"inputName", source.name}, {"inputUuid", source.source_id}, {"audioCapable", true}});
    return {{"inputs", std::move(inputs)}};
  }
  if (type == "GetInputMute")
    return {{"inputMuted", selected_audio_source(state, data).muted}};
  if (type == "GetInputVolume")
  {
    const auto& source = selected_audio_source(state, data);
    return {{"inputVolumeDb", source.volume_db}, {"inputVolumeMul", source.volume_multiplier}};
  }
  if (type == "GetInputAudioMonitorType")
  {
    switch (selected_audio_source(state, data).monitoring_type)
    {
    case 0:
      return {{"monitorType", "OBS_MONITORING_TYPE_NONE"}};
    case 1:
      return {{"monitorType", "OBS_MONITORING_TYPE_MONITOR_ONLY"}};
    case 2:
      return {{"monitorType", "OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT"}};
    default:
      throw std::runtime_error("Streamlabs audio monitoring state is unavailable");
    }
  }
  if (type == "GetInputActiveState")
    return {{"videoActive", false}};
  if (type == "GetStreamStatus")
    return {{"outputActive", output_state_active(state.streaming)},
            {"outputState", output_state_name(state.streaming)}};
  if (type == "GetRecordStatus")
    return {{"outputActive", output_state_active(state.recording)},
            {"outputPaused", false},
            {"outputDuration", 0},
            {"outputTimecode", "00:00:00.000"},
            {"outputState", output_state_name(state.recording)}};
  if (type == "GetStreamServiceSettings")
    return {{"streamServiceSettings", json::object()}, {"configurationKnown", false}};
  if (type == "GetStreamingDestinations")
  {
    json result = json::array();
    if (state.destinations)
      for (const auto& destination : *state.destinations)
        result.push_back({{"destinationId", destination.id},
                          {"backendId", destination.id},
                          {"label", destination.label},
                          {"platformName", destination.platform},
                          {"host", "streamlabs-desktop"},
                          {"serviceKind", destination.service_kind},
                          {"order", result.size()},
                          {"configured", true},
                          {"enabled", destination.enabled},
                          {"state", "unknown"},
                          {"controlScope", "aggregate-only"},
                          {"failureReason", nullptr}});
    return {{"streamingDestinations", std::move(result)}};
  }
  if (type == "GetSceneTransitionList")
    return {{"transitions", json::array()}};
  if (type == "GetCurrentSceneTransition")
    return json::object();
  throw std::runtime_error("Unsupported Streamlabs observation");
}

StreamlabsAdapter::ControlObservation
StreamlabsAdapter::mutation(std::string_view type, const json& data, const State& state)
{
  if (type == "SetCurrentProgramScene")
  {
    if (state.studio_mode_enabled)
      throw std::runtime_error("Streamlabs Program selection is unavailable in Studio Mode");
    const auto& scene = selected_scene(state, data);
    transport_->select_program_scene(scene.id);
    return {ControlKind::program_scene, {}, scene.id};
  }
  if (type == "SetCurrentPreviewScene")
  {
    if (!state.studio_mode_enabled)
      throw std::runtime_error("Streamlabs Preview selection requires Studio Mode");
    const auto& scene = selected_scene(state, data);
    transport_->select_preview_scene(scene.id);
    return {ControlKind::preview_scene, {}, scene.id};
  }
  if (type == "SetSceneItemEnabled")
  {
    const auto& scene = selected_scene(state, data);
    const auto item_id = data.at("sceneItemId").get<std::int64_t>();
    const auto node = std::find_if(scene.nodes.begin(),
                                   scene.nodes.end(),
                                   [&](const SceneNode& candidate)
                                   { return stable_layer_id(candidate.id) == item_id; });
    if (node == scene.nodes.end() || node->type == "folder")
      throw std::out_of_range("Streamlabs scene item was not found or is not controllable");
    if (!canvas_allows_control(state.canvas, node->canvas))
      throw std::runtime_error("Streamlabs Desktop canvas is unavailable");
    const bool visible = data.at("sceneItemEnabled").get<bool>();
    transport_->set_scene_item_visibility(node->resource_id, visible);
    return {ControlKind::scene_item_visibility, node->resource_id, {}, visible};
  }
  if (type == "SetInputMute")
  {
    const auto& source = selected_audio_source(state, data);
    const bool muted = data.at("inputMuted").get<bool>();
    transport_->set_audio_mute(source.resource_id, muted);
    return {ControlKind::audio_mute, source.resource_id, {}, muted};
  }
  if (type == "SetInputVolume")
  {
    const auto& source = selected_audio_source(state, data);
    const auto volume = data.find("inputVolumeDb");
    if (volume == data.end() || !volume->is_number())
      throw std::invalid_argument("Streamlabs audio volume dB is required");
    const auto target = volume->get<double>();
    if (!std::isfinite(target) || target < -100.0 || target > 0.0)
      throw std::out_of_range("Streamlabs audio volume must be within -100..0 dB");
    transport_->set_audio_volume(source.resource_id, target);
    return {ControlKind::audio_volume, source.resource_id, {}, false, target};
  }
  if (type == "StartRecord" || type == "StopRecord")
  {
    const bool target = type == "StartRecord";
    if (output_state_active(state.recording) != target)
      transport_->toggle_recording();
    return {ControlKind::recording, {}, {}, target};
  }
  if (type == "StartStream" || type == "StopStream")
  {
    const bool target = type == "StartStream";
    const auto [recording, streaming] = transport_->read_output_states();
    static_cast<void>(recording);
    if (output_state_active(streaming) != target)
      transport_->toggle_streaming();
    return {ControlKind::streaming, {}, {}, target};
  }
  if (type == "TriggerStudioModeTransition")
  {
    if (!state.studio_mode_enabled)
      throw std::runtime_error("Streamlabs Studio Mode transition requires Studio Mode");
    transport_->execute_studio_transition();
    return {ControlKind::studio_transition, {}, state.preview_scene_id};
  }
  throw std::runtime_error("Unsupported Streamlabs mutation");
}

bool StreamlabsAdapter::observe_control(const ControlObservation& observation)
{
  State replacement;
  {
    std::lock_guard lock(mutex_);
    replacement = state_;
  }

  switch (observation.kind)
  {
  case ControlKind::program_scene:
  {
    {
      std::lock_guard transport_lock(transport_mutex_);
      replacement.active_scene_id = transport_->read_active_scene_id();
    }
    replacement.program_scene_id = replacement.active_scene_id;
    const bool matched = replacement.program_scene_id == observation.desired_scene_id;
    publish(std::move(replacement), true);
    return matched;
  }
  case ControlKind::preview_scene:
  {
    {
      std::lock_guard transport_lock(transport_mutex_);
      replacement.active_scene_id = transport_->read_active_scene_id();
    }
    replacement.preview_scene_id = replacement.active_scene_id;
    const bool matched = replacement.preview_scene_id == observation.desired_scene_id;
    publish(std::move(replacement), true);
    return matched;
  }
  case ControlKind::scene_item_visibility:
  {
    bool visible = false;
    {
      std::lock_guard transport_lock(transport_mutex_);
      visible = transport_->read_scene_item_visibility(observation.resource_id);
    }
    for (auto& scene : replacement.scenes)
      for (auto& node : scene.nodes)
        if (node.resource_id == observation.resource_id)
          node.visible = visible;
    publish(std::move(replacement), true);
    return visible == observation.desired_boolean;
  }
  case ControlKind::audio_mute:
  case ControlKind::audio_volume:
  {
    AudioSource source;
    {
      std::lock_guard transport_lock(transport_mutex_);
      source = transport_->read_audio_source(observation.resource_id);
    }
    for (auto& candidate : replacement.audio_sources)
      if (candidate.resource_id == observation.resource_id)
      {
        candidate.muted = source.muted;
        candidate.volume_db = source.volume_db;
        candidate.volume_multiplier = source.volume_multiplier;
      }
    publish(std::move(replacement), true);
    return observation.kind == ControlKind::audio_mute
               ? source.muted == observation.desired_boolean
               : std::abs(source.volume_db - observation.desired_volume_db) <= 0.05;
  }
  case ControlKind::recording:
  case ControlKind::streaming:
  {
    std::pair<OutputState, OutputState> outputs;
    std::size_t output_revision = 0;
    {
      std::lock_guard lock(mutex_);
      output_revision = output_event_revision_;
    }
    {
      std::lock_guard transport_lock(transport_mutex_);
      outputs = transport_->read_output_states();
    }
    const auto [recording, streaming] = outputs;
    replacement.recording = recording;
    replacement.streaming = streaming;
    publish(std::move(replacement), true, output_revision);
    OutputState observed = OutputState::unknown;
    {
      std::lock_guard lock(mutex_);
      observed = observation.kind == ControlKind::recording ? state_.recording : state_.streaming;
    }
    return output_state_active(observed) == observation.desired_boolean;
  }
  case ControlKind::studio_transition:
  {
    {
      std::lock_guard transport_lock(transport_mutex_);
      replacement.program_scene_id = transport_->read_program_scene_id();
    }
    const bool matched = replacement.program_scene_id == observation.desired_scene_id;
    publish(std::move(replacement), true);
    return matched;
  }
  }
  return false;
}

void StreamlabsAdapter::settle_control(const ControlObservation& observation)
{
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  std::string observation_error;
  do
  {
    try
    {
      if (observe_control(observation))
        return;
    }
    catch (const std::exception& error)
    {
      observation_error = error.what();
    }
    std::this_thread::sleep_for(50ms);
  } while (!stopping_ && std::chrono::steady_clock::now() < deadline);
  if (!observation_error.empty())
  {
    std::lock_guard lock(mutex_);
    last_error_ = "Streamlabs control observation deferred: " + observation_error;
  }
  request_refresh();
}

} // namespace studio_remote::streamlabs
