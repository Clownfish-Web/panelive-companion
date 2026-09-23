#include "obs_state_reconciler.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <studio_remote/state/recording_state.hpp>
#include <studio_remote/state/recording_timecode.hpp>
#include <utility>

namespace studio_remote
{
namespace
{

std::string string_field(const json& value, std::string_view key, std::string fallback = {})
{
  const auto field = value.find(std::string(key));
  if (field == value.end() || !field->is_string())
  {
    return fallback;
  }

  return field->get<std::string>();
}

bool boolean_field(const json& value, std::string_view key, bool fallback = false)
{
  const auto field = value.find(std::string(key));
  if (field == value.end() || !field->is_boolean())
  {
    return fallback;
  }

  return field->get<bool>();
}

std::uint64_t
unsigned_integer_field(const json& value, std::string_view key, std::uint64_t fallback = 0)
{
  const auto field = value.find(std::string(key));
  if (field == value.end() || !field->is_number_integer())
    return fallback;
  const auto result = field->get<std::int64_t>();
  return result >= 0 ? static_cast<std::uint64_t>(result) : fallback;
}

std::uint32_t
unsigned_duration_field(const json& value, std::string_view key, std::uint32_t fallback)
{
  const auto field = value.find(std::string(key));
  if (field == value.end() || !field->is_number())
    return fallback;
  const auto result = field->get<double>();
  if (result < 0.0)
    return fallback;
  return static_cast<std::uint32_t>(result);
}

std::int64_t signed_integer_field(const json& value, std::string_view key)
{
  const auto field = value.find(std::string(key));
  return field != value.end() && field->is_number_integer() ? field->get<std::int64_t>() : 0;
}

void normalize_scene_item(json& item)
{
  if (!item.contains("visibilityStateAvailable"))
    item["visibilityStateAvailable"] =
        item.contains("sceneItemEnabled") && item["sceneItemEnabled"].is_boolean();
  constexpr std::array string_fields{
      "sourceUuid",
      "sourceName",
      "inputKind",
      "sourceType",
  };

  for (const auto* field : string_fields)
  {
    if (!item.contains(field) || !item[field].is_string())
    {
      item[field] = "";
    }
  }

  constexpr std::array boolean_fields{
      "isGroup",
      "sceneItemEnabled",
      "audioCapable",
  };

  for (const auto* field : boolean_fields)
  {
    if (!item.contains(field) || !item[field].is_boolean())
    {
      item[field] = false;
    }
  }
}

void normalize_audio_input(json& input)
{
  if (!input["inputMuted"].is_boolean())
  {
    input["inputMuted"] = false;
  }
  if (!input["inputSolo"].is_boolean())
  {
    input["inputSolo"] = false;
  }
  if (!input["inputRoutedToMaster"].is_boolean())
  {
    input["inputRoutedToMaster"] = false;
  }
  if (!input["videoActive"].is_boolean())
  {
    input["videoActive"] = false;
  }
  if (!input["inputVolumeMul"].is_number())
  {
    input["inputVolumeMul"] = 1.0;
  }
  if (!input["monitoringModeAvailable"].is_boolean())
  {
    input["monitoringModeAvailable"] = false;
  }
}

std::string lower_ascii(std::string value)
{
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](unsigned char character)
                 { return static_cast<char>(std::tolower(character)); });
  return value;
}

std::string host_from_stream_endpoint(std::string value)
{
  if (value.empty())
    return {};
  const auto scheme = value.find("://");
  if (scheme != std::string::npos)
    value.erase(0, scheme + 3);
  if (const auto credentials = value.find('@'); credentials != std::string::npos)
    value.erase(0, credentials + 1);
  if (const auto path = value.find('/'); path != std::string::npos)
    value.erase(path);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.pop_back();
  return value.empty() ? std::string{} : value.substr(0, 256);
}

std::string service_kind_from_host(std::string_view host)
{
  const auto value = lower_ascii(std::string(host));
  if (value.find("youtube") != std::string::npos || value.find("youtu.be") != std::string::npos)
    return "youtube";
  if (value.find("twitch") != std::string::npos)
    return "twitch";
  if (value.find("facebook") != std::string::npos || value.find("fbcdn") != std::string::npos)
    return "facebook";
  if (value.find("kick") != std::string::npos)
    return "kick";
  return value.empty() ? "other" : "custom";
}

std::string stream_state_name(const json& stream_state)
{
  const auto state = string_field(stream_state, "state");
  if (state == "started" || state == "streaming" || boolean_field(stream_state, "active"))
    return "streaming";
  if (state == "starting" || state == "reconnecting" || state == "stopping" || state == "error" ||
      state == "unknown")
    return state;
  return "stopped";
}

} // namespace

ObsStateReconciler::ObsStateReconciler(IObsAdapter& obs,
                                       AudioMixerRoster& audio_mixer_roster,
                                       SceneItemProperties& scene_item_properties,
                                       ISourceMediaInspector* source_media_inspector)
    : obs_(obs), audio_mixer_roster_(audio_mixer_roster),
      scene_item_properties_(scene_item_properties), source_media_inspector_(source_media_inspector)
{
}

json ObsStateReconciler::build_baseline()
{
  json baseline;
  const auto backend = obs_.backend_info();
  baseline["backendId"] = backend.id;
  baseline["systemName"] = backend.display_name;
  baseline["backendCapabilities"] = {
      {"preview", backend.capabilities.preview},
      {"programSelection", backend.capabilities.program_selection},
      {"previewSelection", backend.capabilities.preview_selection},
      {"compositionMonitorRender", backend.capabilities.composition_monitor_render},
      {"layerVisibility", backend.capabilities.layer_visibility},
      {"layerColors", backend.capabilities.layer_colors},
      {"transitionCatalog", backend.capabilities.transition_catalog},
      {"transitionConfigure", backend.capabilities.transition_configure},
      {"transitionExecute", backend.capabilities.transition_execute},
      {"programVideo", backend.capabilities.program_video},
      {"previewVideo", backend.capabilities.preview_video},
      {"thumbnails", backend.capabilities.thumbnails},
      {"audioMeters", backend.capabilities.audio_meters},
      {"audioMonitoring", backend.capabilities.audio_monitoring},
      {"audioMute", backend.capabilities.audio_mute},
      {"audioVolume", backend.capabilities.audio_volume},
      {"audioInputPositiveGain", backend.capabilities.audio_input_positive_gain},
      {"audioSolo", backend.capabilities.audio_solo},
      {"audioInputBalance", backend.capabilities.audio_input_balance},
      {"audioInputMasterRouting", backend.capabilities.audio_input_master_routing},
      {"audioMasterVolume", backend.capabilities.audio_master_volume},
      {"audioMasterMute", backend.capabilities.audio_master_mute},
      {"audioSourceAuditionBus", backend.capabilities.audio_source_audition_bus},
      {"audioVolumeMinimumDb", backend.capabilities.audio_volume_minimum_db},
      {"audioVolumeMaximumDb", backend.capabilities.audio_volume_maximum_db},
      {"audioVolumeFaderCurve", backend.capabilities.audio_volume_fader_curve},
      {"recordingPause", backend.capabilities.recording_pause},
      {"recordingPauseAvailable", obs_.recording_pause_available()},
      {"recordingObserve", backend.capabilities.recording_observe},
      {"recordingStartStop", backend.capabilities.recording_start_stop},
      {"streamingObserve", backend.capabilities.streaming_observe},
      {"streamingStartStop", backend.capabilities.streaming_start_stop},
      {"streamingDestinations", backend.capabilities.streaming_destinations},
      {"streamingMultistream", backend.capabilities.streaming_multistream},
      {"streamingDestinationIndividualControl",
       backend.capabilities.streaming_destination_individual_control},
      {"streamingDestinationMaximum", backend.capabilities.streaming_destination_maximum},
      {"streamConfigurationKnown", backend.capabilities.stream_configuration_known},
      {"quickTransitions", backend.capabilities.quick_transitions},
      {"defaultTransitionUsesQuickPlay", backend.capabilities.default_transition_uses_quick_play},
      {"defaultTransitionStateAvailable", backend.capabilities.default_transition_state_available},
      {"defaultTransitionConfigurationAvailable",
       backend.capabilities.default_transition_configuration_available},
      {"transitionCompletionRequiresVideo",
       backend.capabilities.transition_completion_requires_video},
      {"stableCommandIdentifiersPreferred",
       backend.capabilities.stable_command_identifiers_preferred},
      {"authoritativeControlVerification", backend.capabilities.authoritative_control_verification},
      {"explicitStartStopForRecord", backend.capabilities.explicit_start_stop_for_record},
      {"explicitStartStopForStream", backend.capabilities.explicit_start_stop_for_stream},
      {"performance", backend.capabilities.performance},
      {"performanceMetrics", backend.capabilities.performance_metrics}};

  // A local Companion provider has no external production state to query.
  if (backend.kind == ProductionBackendKind::companion_teleprompter)
    return baseline;

  const auto version = obs_.request("GetVersion");
  baseline["obsVersion"] =
      string_field(version, "obsVersion", string_field(version, "obsWebSocketVersion", "unknown"));

  const auto studio_mode = obs_.request("GetStudioModeEnabled");
  baseline["studioMode"] = boolean_field(studio_mode, "studioModeEnabled");

  try
  {
    const auto transitions = obs_.request("GetSceneTransitionList");
    baseline["transitions"] = transitions.value("transitions", json::array());
    const auto transition = obs_.request("GetCurrentSceneTransition");
    baseline["currentTransitionName"] = string_field(transition, "transitionName");
    baseline["currentTransitionUuid"] = string_field(transition, "transitionUuid");
    baseline["currentTransitionKind"] = string_field(transition, "transitionKind");
    baseline["currentTransitionFixed"] = boolean_field(transition, "transitionFixed");
    baseline["currentTransitionConfigurable"] = boolean_field(transition, "transitionConfigurable");
    baseline["transitionDurationMs"] =
        unsigned_duration_field(transition, "transitionDuration", 1'000U);
    for (auto& catalog_transition : baseline["transitions"])
      if (!catalog_transition.contains("transitionDuration"))
        catalog_transition["transitionDuration"] = baseline["transitionDurationMs"];
  }
  catch (...)
  {
    baseline["transitions"] = json::array();
    baseline["transitionDurationMs"] = 1'000U;
  }

  const auto scene_list = obs_.request("GetSceneList");
  baseline["programSceneName"] = string_field(scene_list, "currentProgramSceneName");
  baseline["previewSceneName"] = string_field(scene_list, "currentPreviewSceneName");
  if (backend.id == "streamlabs")
  {
    // Streamlabs scene names are not authoritative identities. Its internal
    // currentSceneId probe is the only reliable way to distinguish buses when
    // names are duplicated or change between catalog observations.
    baseline["programSceneUuid"] = string_field(scene_list, "currentProgramSceneUuid");
    baseline["previewSceneUuid"] = string_field(scene_list, "currentPreviewSceneUuid");
  }
  baseline["scenes"] = read_scenes(scene_list);
  baseline["inputs"] = read_audio_inputs();
  if (backend.capabilities.audio_master_volume)
    baseline["audioMaster"] = read_audio_master();
  baseline["stream"] = read_stream_state();
  baseline["streamingDestinations"] = read_streaming_destinations(baseline["stream"]);
  baseline["record"] = read_record_state();

  return baseline;
}

json ObsStateReconciler::read_scenes(const json& scene_list_response)
{
  json scenes = json::array();
  auto provider_scenes = scene_list_response.value("scenes", json::array());
  // OBS WebSocket reverses the frontend scene list. Other providers already
  // supply UI order. Scene items separately retain bottom-to-top render order.
  if (obs_.backend_info().id == "obs")
  {
    std::reverse(provider_scenes.begin(), provider_scenes.end());
  }

  for (auto scene : provider_scenes)
  {
    const auto scene_name = string_field(scene, "sceneName");
    const auto scene_uuid = string_field(scene, "sceneUuid");

    scene["sceneName"] = scene_name;
    scene["sceneUuid"] = scene_uuid;
    scene["sceneItems"] = read_scene_items(scene_name, scene_uuid);
    scenes.push_back(std::move(scene));
  }

  return scenes;
}

json ObsStateReconciler::read_scene_items(const std::string& scene_name,
                                          const std::string& scene_uuid)
{
  // Scene names are supported by every obs-websocket 5.x release. Sending a
  // name and UUID together is rejected by strict releases because they are
  // alternative selectors.
  const auto backend = obs_.backend_info();
  const json selector =
      backend.id == "obs" ? json{{"sceneName", scene_name}} : json{{"sceneUuid", scene_uuid}};
  auto response = obs_.request("GetSceneItemList", selector);

  if (!response.contains("sceneItems") || !response["sceneItems"].is_array())
  {
    throw std::runtime_error("OBS returned an invalid scene item list for " + scene_name);
  }

  auto items = std::move(response["sceneItems"]);
  for (auto& item : items)
  {
    normalize_scene_item(item);
    classify_media_source(item);
    if (backend.id == "obs")
    {
      const auto item_id = signed_integer_field(item, "sceneItemId");
      const auto color = scene_item_properties_.color(scene_uuid, item_id);
      item["color"] = color ? json(*color) : json(nullptr);
    }
    else if (!item.contains("color"))
      item["color"] = nullptr;

    if (boolean_field(item, "isGroup"))
    {
      item["groupSceneItems"] =
          read_group_items(string_field(item, "sourceName"), string_field(item, "sourceUuid"));
    }
  }

  return items;
}

json ObsStateReconciler::read_group_items(const std::string& group_name,
                                          const std::string& group_uuid,
                                          std::size_t depth)
{
  if (depth >= 64)
    throw std::runtime_error("OBS group hierarchy exceeds the supported depth");
  json selector = {{"sceneName", group_name}};
  if (obs_.backend_info().id != "obs" && !group_uuid.empty())
    selector["groupUuid"] = group_uuid;
  auto response = obs_.request("GetGroupSceneItemList", std::move(selector));
  if (!response.contains("sceneItems") || !response["sceneItems"].is_array())
    throw std::runtime_error("OBS returned an invalid group item list");
  auto items = std::move(response["sceneItems"]);
  const auto backend = obs_.backend_info();
  for (auto& item : items)
  {
    normalize_scene_item(item);
    classify_media_source(item);
    if (backend.id == "obs")
    {
      const auto color =
          scene_item_properties_.color(group_uuid, signed_integer_field(item, "sceneItemId"));
      item["color"] = color ? json(*color) : json(nullptr);
    }
    else if (!item.contains("color"))
      item["color"] = nullptr;
    if (boolean_field(item, "isGroup"))
      item["groupSceneItems"] = read_group_items(
          string_field(item, "sourceName"), string_field(item, "sourceUuid"), depth + 1);
  }
  return items;
}

void ObsStateReconciler::classify_media_source(json& item)
{
  item["visualCapability"] = "unknown";
  const auto transform = item.find("sceneItemTransform");
  if (transform != item.end() && transform->is_object() &&
      unsigned_integer_field(*transform, "sourceWidth") > 0 &&
      unsigned_integer_field(*transform, "sourceHeight") > 0)
  {
    item["visualCapability"] = "visual";
    return;
  }

  if (string_field(item, "inputKind") != "ffmpeg_source" || !source_media_inspector_)
    return;

  json selector;
  const auto source_uuid = string_field(item, "sourceUuid");
  if (!source_uuid.empty())
    selector["inputUuid"] = source_uuid;
  else
    selector["inputName"] = string_field(item, "sourceName");

  try
  {
    const auto response = obs_.request("GetInputSettings", selector);
    const auto settings = response.find("inputSettings");
    if (settings == response.end() || !settings->is_object() ||
        !boolean_field(*settings, "is_local_file"))
      return;

    const auto path = string_field(*settings, "local_file");
    if (path.empty())
      return;

    item["visualCapability"] =
        source_visual_capability_name(source_media_inspector_->inspect({path}));
  }
  catch (const std::exception&)
  {
    // Missing settings or unsupported media metadata is an authoritative unknown.
  }
}

json ObsStateReconciler::read_audio_inputs()
{
  constexpr auto initial_roster_wait = std::chrono::milliseconds(500);
  const auto response = obs_.request("GetInputList");
  const auto backend = obs_.backend_info();
  if (!backend.capabilities.native_audio_roster_required)
  {
    std::vector<AudioMixerMember> members;
    for (const auto& input : response.value("inputs", json::array()))
    {
      if (!input.value("audioCapable", false))
        continue;
      const auto input_uuid = string_field(input, "inputUuid");
      const auto input_name = string_field(input, "inputName");
      if (!input_uuid.empty() && !input_name.empty())
        members.push_back({input_uuid, input_name, 0});
    }
    audio_mixer_roster_.replace(std::move(members));
  }
  const auto mixer_roster = audio_mixer_roster_.wait_for_first_observation(initial_roster_wait);
  json inputs = json::array();

  if (!mixer_roster.observed)
  {
    return inputs;
  }

  std::unordered_map<std::string, json> metadata_by_uuid;
  for (auto input : response.value("inputs", json::array()))
  {
    input["inputName"] = string_field(input, "inputName");
    input["inputUuid"] = string_field(input, "inputUuid");
    const auto input_uuid = input["inputUuid"].get<std::string>();
    if (!input_uuid.empty())
      metadata_by_uuid.insert_or_assign(input_uuid, std::move(input));
  }

  for (const auto& [input_uuid, member] : mixer_roster.inputs_by_uuid)
  {
    auto metadata = metadata_by_uuid.find(input_uuid);
    json input = metadata == metadata_by_uuid.end()
                     ? json{{"inputName", member.input_name}, {"inputUuid", input_uuid}}
                     : metadata->second;
    input["inputName"] = member.input_name;
    input["inputUuid"] = input_uuid;

    const json selector = {{"inputUuid", input_uuid}};

    try
    {
      input.update(obs_.request("GetInputMute", selector));
      input.update(obs_.request("GetInputVolume", selector));
      if (backend.capabilities.audio_solo)
        input.update(obs_.request("GetInputSolo", selector));
      if (backend.capabilities.audio_input_balance)
        input.update(obs_.request("GetInputBalance", selector));

      try
      {
        const auto monitoring = obs_.request("GetInputAudioMonitorType", selector);
        if (monitoring.contains("monitorType") && monitoring["monitorType"].is_string())
        {
          input["monitorType"] = monitoring["monitorType"];
          input["monitoringModeAvailable"] = true;
        }
      }
      catch (...)
      {
        input["monitoringModeAvailable"] = false;
      }

      try
      {
        input.update(obs_.request("GetInputActiveState", selector));
      }
      catch (...)
      {
        input["videoActive"] = false;
      }

      normalize_audio_input(input);
    }
    catch (...)
    {
      // Membership comes from OBS's native mixer. A transient metadata request
      // failure must not remove a genuine, possibly silent or muted, mixer row.
      normalize_audio_input(input);
    }
    inputs.push_back(std::move(input));
  }

  return inputs;
}

json ObsStateReconciler::read_audio_master()
{
  return obs_.request("GetMasterVolume");
}

json ObsStateReconciler::read_stream_state()
{
  const auto response = obs_.request("GetStreamStatus");
  const bool active = boolean_field(response, "outputActive");
  bool configuration_known = obs_.backend_info().capabilities.stream_configuration_known;
  std::string host;
  std::string service_kind = "other";
  bool configured = false;
  try
  {
    const auto settings_response = obs_.request("GetStreamServiceSettings");
    configuration_known = settings_response.value("configurationKnown", configuration_known);
    const auto settings = settings_response.value("streamServiceSettings", json::object());
    for (const auto* field : {"server", "url", "endpoint"})
    {
      const auto candidate = host_from_stream_endpoint(string_field(settings, field));
      if (!candidate.empty())
      {
        host = candidate;
        break;
      }
    }
    configured = !host.empty();
    service_kind = service_kind_from_host(host);
  }
  catch (...)
  {
    configured = !configuration_known;
  }

  return {
      {"active", active},
      {"state", active ? "started" : "stopped"},
      {"configured", configured},
      {"configurationKnown", configuration_known},
      {"host", host.empty() ? "not-configured" : host},
      {"serviceKind", service_kind},
  };
}

json ObsStateReconciler::read_streaming_destinations(const json& stream_state)
{
  if (!obs_.backend_info().capabilities.streaming_destinations)
    return json::array();

  try
  {
    const auto response = obs_.request("GetStreamingDestinations");
    const auto destinations = response.find("streamingDestinations");
    if (destinations != response.end() && destinations->is_array())
      return *destinations;
  }
  catch (...)
  {
  }

  const bool configured = stream_state.value("configured", false);
  const auto host = string_field(stream_state, "host", "not-configured");
  const auto service_kind = string_field(stream_state, "serviceKind", service_kind_from_host(host));
  return json::array({{{"destinationId", "primary-stream"},
                       {"backendId", "primary-stream"},
                       {"label", "Primary Stream"},
                       {"host", host.empty() ? "not-configured" : host},
                       {"serviceKind", service_kind},
                       {"order", 0},
                       {"configured", configured},
                       {"enabled", configured || !stream_state.value("configurationKnown", true)},
                       {"state", stream_state_name(stream_state)},
                       {"controlScope", "aggregate-only"},
                       {"failureReason", nullptr}}});
}

json ObsStateReconciler::read_record_state()
{
  const auto response = obs_.request("GetRecordStatus");
  const auto normalized = normalize_recording_state(boolean_field(response, "outputActive"),
                                                    boolean_field(response, "outputPaused"));
  const auto duration_ms =
      normalized.active ? unsigned_integer_field(response, "outputDuration") : 0;
  auto timecode = string_field(response, "outputTimecode");
  if (!normalized.active || !valid_recording_timecode(timecode))
    timecode = format_recording_timecode(duration_ms);

  return {
      {"active", normalized.active},
      {"paused", normalized.paused},
      {"state", normalized.state},
      {"timecode", timecode},
      {"durationMs", duration_ms},
      {"observedAtMonotonicNs", monotonic_ns()},
  };
}

bool ObsStateReconciler::has_configured_stream_destination()
{
  try
  {
    const auto response = obs_.request("GetStreamServiceSettings");
    const auto settings = response.value("streamServiceSettings", json::object());

    for (const auto* field : {"server", "url", "endpoint"})
    {
      if (settings.contains(field) && settings[field].is_string() &&
          !settings[field].get<std::string>().empty())
      {
        return true;
      }
    }
  }
  catch (...)
  {
    return false;
  }

  return false;
}

} // namespace studio_remote
