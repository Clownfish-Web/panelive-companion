#include <studio_remote/streamlabs/streamlabs_protocol.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <studio_remote/protocol/provider_json.hpp>
#include <unordered_set>

namespace studio_remote::streamlabs
{
namespace
{

constexpr std::array allowed_events{
    std::string_view{"SceneCollectionsService.collectionSwitched"},
    std::string_view{"ScenesService.itemAdded"},
    std::string_view{"ScenesService.itemRemoved"},
    std::string_view{"ScenesService.itemUpdated"},
    std::string_view{"ScenesService.sceneAdded"},
    std::string_view{"ScenesService.sceneRemoved"},
    std::string_view{"ScenesService.sceneSwitched"},
    std::string_view{"StreamingService.recordingStatusChange"},
    std::string_view{"StreamingService.streamingStatusChange"},
    std::string_view{"TransitionsService.studioModeChanged"},
    std::string_view{"DualOutputService.dualOutputModeChanged"},
    std::string_view{"DualOutputService.displayToggled"},
};

std::string required_string(const json& value, std::string_view field)
{
  const auto found = value.find(std::string(field));
  if (found == value.end() || !found->is_string())
    throw std::runtime_error("Streamlabs response is missing a required string");
  auto result = found->get<std::string>();
  if (result.empty() || result.size() > maximum_string_bytes)
    throw std::runtime_error("Streamlabs response contains an invalid string length");
  return result;
}

bool required_boolean(const json& value, std::string_view field)
{
  const auto found = value.find(std::string(field));
  if (found == value.end() || !found->is_boolean())
    throw std::runtime_error("Streamlabs response is missing a required boolean");
  return found->get<bool>();
}

double finite_number(const json& value, std::string_view field)
{
  const auto found = value.find(std::string(field));
  if (found == value.end() || !found->is_number())
    throw std::runtime_error("Streamlabs response is missing a required number");
  const auto result = found->get<double>();
  if (!std::isfinite(result))
    throw std::runtime_error("Streamlabs response contains a non-finite number");
  return result;
}

std::optional<double> optional_finite_number(const json& value, std::string_view field)
{
  const auto found = value.find(std::string(field));
  if (found == value.end() || found->is_null())
    return std::nullopt;
  if (!found->is_number())
    throw std::runtime_error("Streamlabs performance metric is not numeric: " + std::string(field));
  const auto result = found->get<double>();
  if (!std::isfinite(result))
    throw std::runtime_error("Streamlabs performance metric is non-finite: " + std::string(field));
  return result;
}

void require_array_limit(const json& value, std::size_t limit, std::string_view description)
{
  if (!value.is_array() || value.size() > limit)
    throw std::runtime_error("Streamlabs returned an invalid " + std::string(description));
}

bool dynamic_resource(std::string_view resource, std::string_view prefix)
{
  return resource.starts_with(prefix) && resource.size() <= maximum_string_bytes &&
         resource.ends_with(']');
}

} // namespace

OutputState parse_output_state(std::string_view value) noexcept
{
  if (value == "offline")
    return OutputState::offline;
  if (value == "starting")
    return OutputState::starting;
  if (value == "live" || value == "recording" || value == "active")
    return OutputState::active;
  if (value == "ending" || value == "stopping")
    return OutputState::stopping;
  if (value == "reconnecting")
    return OutputState::reconnecting;
  if (value == "error")
    return OutputState::error;
  return OutputState::unknown;
}

json make_request(std::uint64_t id,
                  std::string_view resource,
                  std::string_view method,
                  json arguments)
{
  if (!allowed_method(resource, method))
    throw std::invalid_argument("Streamlabs resource or method is outside the allowlist");
  if (!arguments.is_array() || arguments.size() > maximum_catalog_entries)
    throw std::invalid_argument("Streamlabs request arguments are invalid");
  return {{"jsonrpc", "2.0"},
          {"id", id},
          {"method", method},
          {"params", {{"resource", resource}, {"args", std::move(arguments)}}}};
}

RpcMessage parse_message(std::string_view message)
{
  if (message.empty() || message.size() > maximum_message_bytes)
    throw std::runtime_error("Streamlabs message size is invalid");

  json parsed;
  try
  {
    parsed = parse_provider_json_object(message, maximum_message_bytes);
  }
  catch (const std::invalid_argument&)
  {
    throw std::runtime_error("Streamlabs returned malformed JSON");
  }
  if (!parsed.is_object() || parsed.value("jsonrpc", std::string{}) != "2.0")
    throw std::runtime_error("Streamlabs returned an invalid JSON-RPC envelope");

  if (const auto error = parsed.find("error"); error != parsed.end())
    throw std::runtime_error(sanitize_protocol_error(error->dump()));

  const auto payload = parsed.find("result");
  if (payload == parsed.end())
    throw std::runtime_error("Streamlabs response has no result");

  RpcMessage result;
  if (payload->is_object() && payload->value("_type", std::string{}) == "EVENT")
  {
    result.event_resource = required_string(*payload, "resourceId");
    const auto emitter = required_string(*payload, "emitter");
    if (emitter == "STREAM")
    {
      if (std::find(allowed_events.begin(), allowed_events.end(), result.event_resource) ==
          allowed_events.end())
        throw std::runtime_error("Streamlabs returned an event outside the allowlist");
    }
    else if (emitter == "PROMISE")
    {
      result.promise_event = true;
      result.promise_rejected = required_boolean(*payload, "isRejected");
    }
    else
    {
      throw std::runtime_error("Streamlabs returned an event with an invalid emitter");
    }
    const auto data = payload->find("data");
    result.event_data = data == payload->end() ? json{} : *data;
    result.event = true;
    return result;
  }

  if (const auto id = parsed.find("id"); id != parsed.end())
  {
    if (!id->is_number_unsigned() && (!id->is_number_integer() || id->get<std::int64_t>() < 0))
      throw std::runtime_error("Streamlabs returned an invalid correlation ID");
    result.id = id->get<std::uint64_t>();
  }
  if (!result.id)
    throw std::runtime_error("Streamlabs response has no correlation ID");
  result.result = *payload;
  return result;
}

std::optional<std::string> promise_subscription_id(const json& value)
{
  if (!value.is_object() || value.value("_type", std::string{}) != "SUBSCRIPTION" ||
      value.value("emitter", std::string{}) != "PROMISE")
    return std::nullopt;
  return required_string(value, "resourceId");
}

PromiseEventDisposition classify_promise_event(const RpcMessage& message,
                                               std::string_view expected_resource) noexcept
{
  if (!message.promise_event || expected_resource.empty() ||
      message.event_resource != expected_resource)
    return PromiseEventDisposition::unrelated;
  return message.promise_rejected ? PromiseEventDisposition::rejected
                                  : PromiseEventDisposition::resolved;
}

SceneCollection parse_active_collection(const json& value)
{
  if (!value.is_object())
    throw std::runtime_error("Streamlabs returned an invalid active scene collection");
  return {required_string(value, "id"), required_string(value, "name")};
}

std::vector<Scene> parse_scenes(const json& value)
{
  require_array_limit(value, maximum_catalog_entries, "scene catalog");
  std::vector<Scene> scenes;
  std::unordered_set<std::string> scene_ids;
  scenes.reserve(value.size());
  for (const auto& raw_scene : value)
  {
    if (!raw_scene.is_object())
      throw std::runtime_error("Streamlabs scene catalog contains a non-object");
    Scene scene{required_string(raw_scene, "id"), required_string(raw_scene, "name"), {}};
    if (!scene_ids.insert(scene.id).second)
      throw std::runtime_error("Streamlabs scene catalog contains duplicate IDs");
    const auto nodes = raw_scene.find("nodes");
    if (nodes == raw_scene.end())
      throw std::runtime_error("Streamlabs scene has no complete node list");
    require_array_limit(*nodes, maximum_nodes_per_scene, "scene node list");
    std::unordered_set<std::string> node_ids;
    scene.nodes.reserve(nodes->size());
    for (const auto& raw_node : *nodes)
    {
      if (!raw_node.is_object())
        throw std::runtime_error("Streamlabs scene node is not an object");
      SceneNode node;
      node.id = required_string(raw_node, "id");
      node.scene_id = required_string(raw_node, "sceneId");
      node.source_id = required_string(raw_node, "sourceId");
      node.name = required_string(raw_node, "name");
      node.resource_id = required_string(raw_node, "resourceId");
      node.type = required_string(raw_node, "sceneNodeType");
      node.visible = required_boolean(raw_node, "visible");
      if (const auto parent = raw_node.find("parentId"); parent != raw_node.end())
      {
        if (!parent->is_string() ||
            parent->get_ref<const std::string&>().size() > maximum_string_bytes)
          throw std::runtime_error("Streamlabs scene node has an invalid parent ID");
        node.parent_id = parent->get<std::string>();
      }
      if (node.scene_id != scene.id || !node_ids.insert(node.id).second)
        throw std::runtime_error("Streamlabs scene node identity is inconsistent");
      scene.nodes.push_back(std::move(node));
    }
    scenes.push_back(std::move(scene));
  }
  return scenes;
}

std::string parse_active_scene_id(const json& value)
{
  if (value.is_string())
  {
    if (value.get_ref<const std::string&>().empty() ||
        value.get_ref<const std::string&>().size() > maximum_string_bytes)
      throw std::runtime_error("Streamlabs active scene ID is invalid");
    return value.get<std::string>();
  }
  if (value.is_object())
    return required_string(value, "id");
  throw std::runtime_error("Streamlabs returned an invalid active scene");
}

bool parse_studio_mode(const json& value)
{
  if (!value.is_object())
    throw std::runtime_error("Streamlabs returned an invalid Studio Mode state");
  return required_boolean(value, "studioMode");
}

PerformanceState parse_performance_state(const json& value)
{
  if (!value.is_object())
    throw std::runtime_error("Streamlabs returned an invalid performance state");
  PerformanceState result;
  result.cpu_usage_percent = optional_finite_number(value, "CPU");
  result.bandwidth_kbps = optional_finite_number(value, "bandwidth");
  result.frame_rate = optional_finite_number(value, "frameRate");
  const auto dropped_frames = optional_finite_number(value, "numberDroppedFrames");
  result.dropped_frames_percent = optional_finite_number(value, "percentageDroppedFrames");
  if ((result.cpu_usage_percent &&
       (*result.cpu_usage_percent < 0.0 || *result.cpu_usage_percent > 100.0)) ||
      (result.bandwidth_kbps && *result.bandwidth_kbps < 0.0) ||
      (result.frame_rate && *result.frame_rate < 0.0) ||
      (dropped_frames &&
       (*dropped_frames < 0.0 || std::floor(*dropped_frames) != *dropped_frames ||
        *dropped_frames >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()))) ||
      (result.dropped_frames_percent &&
       (*result.dropped_frames_percent < 0.0 || *result.dropped_frames_percent > 100.0)))
    throw std::runtime_error("Streamlabs returned performance values outside their valid ranges");
  if (dropped_frames)
    result.dropped_frames = static_cast<std::uint64_t>(*dropped_frames);
  return result;
}

std::vector<AudioSource> parse_audio_sources(const json& value)
{
  require_array_limit(value, maximum_catalog_entries, "audio source catalog");
  std::vector<AudioSource> sources;
  std::unordered_set<std::string> source_ids;
  sources.reserve(value.size());
  for (const auto& raw_source : value)
  {
    if (!raw_source.is_object())
      throw std::runtime_error("Streamlabs audio source is not an object");
    if (const auto hidden = raw_source.find("mixerHidden"); hidden != raw_source.end())
    {
      if (!hidden->is_boolean())
        throw std::runtime_error("Streamlabs audio source has an invalid mixer visibility");
      if (hidden->get<bool>())
        continue;
    }
    const auto fader = raw_source.find("fader");
    if (fader == raw_source.end() || !fader->is_object())
      throw std::runtime_error("Streamlabs audio source has no authoritative fader");
    AudioSource source;
    source.source_id = required_string(raw_source, "sourceId");
    source.name = required_string(raw_source, "name");
    source.resource_id = required_string(raw_source, "resourceId");
    source.muted = required_boolean(raw_source, "muted");
    source.volume_db = finite_number(*fader, "db");
    source.volume_multiplier = finite_number(*fader, "mul");
    if (const auto monitoring = raw_source.find("monitoringType"); monitoring != raw_source.end())
    {
      if (!monitoring->is_number_integer())
        throw std::runtime_error("Streamlabs audio source has an invalid monitoring mode");
      source.monitoring_type = monitoring->get<int>();
      if (source.monitoring_type < 0 || source.monitoring_type > 2)
        throw std::runtime_error("Streamlabs audio source has an invalid monitoring mode");
    }
    if (const auto mixers = raw_source.find("audioMixers"); mixers != raw_source.end())
    {
      if (!mixers->is_number_unsigned() &&
          (!mixers->is_number_integer() || mixers->get<std::int64_t>() < 0))
        throw std::runtime_error("Streamlabs audio source has invalid mixer routing");
      source.audio_mixers = mixers->get<std::uint32_t>();
    }
    if (source.volume_multiplier < 0.0 || !source_ids.insert(source.source_id).second)
      throw std::runtime_error("Streamlabs audio source identity or fader is invalid");
    sources.push_back(std::move(source));
  }
  return sources;
}

AudioSource parse_audio_source(const json& value, std::string_view resource_id)
{
  if (!value.is_object())
    throw std::runtime_error("Streamlabs audio source is not an object");
  if (resource_id.empty() || resource_id.size() > maximum_string_bytes)
    throw std::invalid_argument("Streamlabs audio source resource identity is invalid");
  const auto fader = value.find("fader");
  if (fader == value.end() || !fader->is_object())
    throw std::runtime_error("Streamlabs audio source has no authoritative fader");
  AudioSource source;
  source.source_id = required_string(value, "sourceId");
  source.name = required_string(value, "name");
  source.resource_id = std::string(resource_id);
  source.muted = required_boolean(value, "muted");
  source.volume_db = finite_number(*fader, "db");
  source.volume_multiplier = finite_number(*fader, "mul");
  if (source.volume_multiplier < 0.0)
    throw std::runtime_error("Streamlabs audio source fader is invalid");
  return source;
}

bool parse_scene_item_visibility(const json& value)
{
  if (!value.is_object())
    throw std::runtime_error("Streamlabs scene item is not an object");
  return required_boolean(value, "visible");
}

double volume_db_to_deflection(double volume_db) noexcept
{
  if (volume_db >= 0.0)
    return 1.0;
  if (volume_db >= -9.0)
    return (volume_db + 9.0) / 9.0 * 0.25 + 0.75;
  if (volume_db >= -20.0)
    return (volume_db + 20.0) / 11.0 * 0.25 + 0.5;
  if (volume_db >= -30.0)
    return (volume_db + 30.0) / 10.0 * 0.2 + 0.3;
  if (volume_db >= -40.0)
    return (volume_db + 40.0) / 10.0 * 0.15 + 0.15;
  if (volume_db >= -50.0)
    return (volume_db + 50.0) / 10.0 * 0.075 + 0.075;
  if (volume_db >= -60.0)
    return (volume_db + 60.0) / 10.0 * 0.05 + 0.025;
  if (volume_db > -100.0)
    return (volume_db + 100.0) / 40.0 * 0.025;
  return 0.0;
}

std::pair<OutputState, OutputState> parse_output_states(const json& value)
{
  if (!value.is_object())
    throw std::runtime_error("Streamlabs returned an invalid output state");
  return {parse_output_state(required_string(value, "recordingStatus")),
          parse_output_state(required_string(value, "streamingStatus"))};
}

std::string sanitize_protocol_error(std::string_view value)
{
  static constexpr std::array sensitive_markers{"token",
                                                "authorization",
                                                "cookie",
                                                "streamkey",
                                                "stream_key",
                                                "ingest",
                                                "account",
                                                "channel",
                                                "userid",
                                                "user_id",
                                                "profile",
                                                "settings",
                                                "path"};
  std::string lower(value);
  std::transform(lower.begin(),
                 lower.end(),
                 lower.begin(),
                 [](unsigned char character)
                 { return static_cast<char>(std::tolower(character)); });
  for (const auto* marker : sensitive_markers)
    if (lower.find(marker) != std::string::npos)
      return "Streamlabs request failed with a redacted backend error";
  return "Streamlabs request failed";
}

bool allowed_resource(std::string_view resource) noexcept
{
  return resource == "SourcesService" || resource == "DualOutputService" ||
         resource == "HotkeysService" || resource == "StreamSettingsService" ||
         resource == "SceneCollectionsService" || resource == "ScenesService" ||
         resource == "AudioService" || resource == "StreamingService" ||
         resource == "TransitionsService" || resource == "PerformanceService" ||
         dynamic_resource(resource, "Scene[") || dynamic_resource(resource, "SceneItem[") ||
         dynamic_resource(resource, "AudioSource[");
}

bool allowed_method(std::string_view resource, std::string_view method) noexcept
{
  if (!allowed_resource(resource))
    return false;
  if (resource == "SourcesService")
    return method == "getSources";
  if (resource == "DualOutputService")
    return method == "state" || method == "dualOutputModeChanged" || method == "displayToggled";
  if (resource == "HotkeysService")
    return method == "getHotkeysSet";
  if (resource == "StreamSettingsService")
    return method == "state";
  if (resource == "SceneCollectionsService")
    return method == "collections" || method == "load" || method == "activeCollection" ||
           method == "collectionSwitched";
  if (resource == "ScenesService")
    return method == "getScenes" || method == "activeScene" || method == "activeSceneId" ||
           method == "makeSceneActive" || method == "sceneSwitched" || method == "itemAdded" ||
           method == "itemRemoved" || method == "itemUpdated" || method == "sceneAdded" ||
           method == "sceneRemoved";
  if (resource == "AudioService")
    return method == "getSources" || method == "getSourcesForCurrentScene";
  if (resource == "StreamingService")
    return method == "getModel" || method == "toggleRecording" || method == "toggleStreaming" ||
           method == "recordingStatusChange" || method == "streamingStatusChange";
  if (resource == "TransitionsService")
    return method == "getModel" || method == "currentSceneId" ||
           method == "executeStudioModeTransition" || method == "studioModeChanged" ||
           method == "enableStudioMode" || method == "disableStudioMode";
  if (resource == "PerformanceService")
    return method == "getModel";
  if (dynamic_resource(resource, "SceneItem["))
    return method == "getModel" || method == "setVisibility";
  if (dynamic_resource(resource, "AudioSource["))
    return method == "getModel" || method == "setMuted" || method == "setDeflection";
  return dynamic_resource(resource, "Scene[") && (method == "getModel" || method == "getItems");
}

std::span<const std::string_view> event_allowlist() noexcept
{
  return allowed_events;
}

} // namespace studio_remote::streamlabs
