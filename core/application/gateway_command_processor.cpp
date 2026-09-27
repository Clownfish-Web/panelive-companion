#include "gateway_command_processor.hpp"
#include "command_authorization.hpp"
#include <studio_remote/common/bounded_json_size.hpp>

#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace studio_remote
{
namespace
{

constexpr auto command_cache_lifetime = std::chrono::minutes(10);
constexpr auto command_completion_timeout = std::chrono::seconds(12);
constexpr auto completion_poll_interval = std::chrono::milliseconds(50);
constexpr std::size_t operation_queue_capacity = 128;
constexpr double minimum_obs_volume_db = -100.0;
constexpr double maximum_obs_volume_db = 26.0;
constexpr double volume_completion_tolerance_db = 0.001;

ControlStateExpectation boolean_expectation(std::string control_id,
                                            std::string request_type,
                                            json request_data,
                                            std::string response_field,
                                            bool expected)
{
  return {std::move(control_id),
          std::move(request_type),
          std::move(request_data),
          std::move(response_field),
          expected};
}

ControlStateExpectation string_expectation(std::string control_id,
                                           std::string request_type,
                                           json request_data,
                                           std::string response_field,
                                           std::string expected)
{
  return {std::move(control_id),
          std::move(request_type),
          std::move(request_data),
          std::move(response_field),
          std::move(expected)};
}

ControlStateExpectation number_expectation(std::string control_id,
                                           std::string request_type,
                                           json request_data,
                                           std::string response_field,
                                           double expected,
                                           double tolerance)
{
  return {std::move(control_id),
          std::move(request_type),
          std::move(request_data),
          std::move(response_field),
          expected,
          tolerance};
}

const std::unordered_map<std::string, std::string> output_requests = {
    {"start-stream", "StartStream"},
    {"stop-stream", "StopStream"},
    {"start-record", "StartRecord"},
    {"stop-record", "StopRecord"},
    {"pause-record", "PauseRecord"},
    {"resume-record", "ResumeRecord"},
};

std::optional<std::string> required_string(const json& body, std::string_view name)
{
  const auto found = body.find(std::string(name));
  if (found == body.end() || !found->is_string())
    return std::nullopt;
  auto value = found->get<std::string>();
  return value.empty() ? std::nullopt : std::optional<std::string>(std::move(value));
}

std::optional<bool> required_boolean(const json& body, std::string_view name)
{
  const auto found = body.find(std::string(name));
  return found != body.end() && found->is_boolean() ? std::optional<bool>(found->get<bool>())
                                                    : std::nullopt;
}

std::optional<double> required_obs_volume_db(const json& body)
{
  const auto found = body.find("volumeDb");
  if (found == body.end() || !found->is_number())
    return std::nullopt;
  const auto value = found->get<double>();
  if (!std::isfinite(value) || value < minimum_obs_volume_db || value > maximum_obs_volume_db)
    return std::nullopt;
  return value;
}

std::optional<double> required_balance(const json& body)
{
  const auto found = body.find("balance");
  if (found == body.end() || !found->is_number())
    return std::nullopt;
  const auto value = found->get<double>();
  return std::isfinite(value) && value >= -1.0 && value <= 1.0 ? std::optional<double>(value)
                                                               : std::nullopt;
}

double quantized_volume_db(double volume_db, const ProductionBackendCapabilities& backend)
{
  if (volume_db <= minimum_obs_volume_db || backend.audio_volume_step_percent == 0)
    return volume_db;
  const auto exponent = std::max(backend.audio_volume_fader_exponent, 1.0);
  const auto amplitude = std::pow(10.0, volume_db / 20.0);
  const auto percentage = std::clamp(
      std::round(std::pow(amplitude, 1.0 / exponent) * 100.0 / backend.audio_volume_step_percent) *
          backend.audio_volume_step_percent,
      0.0,
      100.0);
  return percentage <= 0.0 ? -100.0 : 20.0 * exponent * std::log10(percentage / 100.0);
}

std::optional<std::int64_t> required_integer(const json& body, std::string_view name)
{
  const auto found = body.find(std::string(name));
  if (found == body.end() || !found->is_number_integer())
    return std::nullopt;
  if (found->is_number_unsigned())
  {
    const auto value = found->get<std::uint64_t>();
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
      return std::nullopt;
    return static_cast<std::int64_t>(value);
  }

  const auto value = found->get<std::int64_t>();
  return value >= 0 ? std::optional<std::int64_t>(value) : std::nullopt;
}

} // namespace

GatewayCommandProcessor::GatewayCommandProcessor(IObsAdapter& obs,
                                                 StateStore& state,
                                                 AuditStore& audit,
                                                 ReconcileRequest request_reconcile,
                                                 ReceiptClock receipt_clock)
    : obs_(obs), state_(state), audit_(audit), request_reconcile_(std::move(request_reconcile)),
      receipt_clock_(std::move(receipt_clock))
{
  operation_thread_ = std::thread([this] { operation_loop(); });
}

GatewayCommandProcessor::~GatewayCommandProcessor()
{
  stop();
}

json GatewayCommandProcessor::execute(const Device& device,
                                      std::string_view action,
                                      const json& body)
{
  const auto command_id_value = required_string(body, "commandId");
  if (!command_id_value || command_id_value->size() > 128)
  {
    return command_result(body, "rejected", "valid commandId required");
  }
  const auto& command_id = *command_id_value;

  if (!authorized(device))
  {
    audit_.command(device.id, action, "", "rejected", command_id);
    auto rejected = command_result(body, "rejected", "operator role required");
    rejected["reasonCode"] = "permission-required";
    return rejected;
  }
  const auto command_key =
      device.id + ":" + std::to_string(device.pairing_generation) + ":" + command_id;
  if (auto cached = cached_response(command_key))
  {
    return *cached;
  }

  auto command_body = body;
  command_body.erase("expectedRevision");
  command_body.erase("expectedSceneItemRevision");

  const auto command_state = state_.command_state_snapshot();
  const auto evaluated_revision = command_state.revision();
  if (!command_state.fresh())
  {
    audit_.command(device.id, action, "", "rejected", command_id);
    return command_result(body, "rejected", "OBS state is stale");
  }
  auto prepared = prepare_command(action, command_body, command_state);
  if (auto* failure = std::get_if<CommandFailure>(&prepared))
  {
    audit_.command(device.id, action, "", "command_semantic_precondition_failed", command_id);
    auto response = failure->response;
    response["reasonCode"] = failure_reason(failure->code);
    return response;
  }

  auto plan = std::get<CommandPlan>(std::move(prepared));
  const auto admission_verification = verify_control_state(plan);
  if (admission_verification != ControlStateVerification::matched)
  {
    request_reconcile_();
    audit_.command(
        device.id, action, plan.audit_target, "command_authoritative_state_rejected", command_id);
    auto response = command_result(
        body,
        admission_verification == ControlStateVerification::changed ? "conflict" : "rejected",
        admission_verification == ControlStateVerification::changed
            ? "production state changed before command admission"
            : "production state could not be verified before command admission");
    response["reasonCode"] = admission_verification == ControlStateVerification::changed
                                 ? "invalid-command-state"
                                 : "backend-unavailable";
    return response;
  }
  plan.completion_data["_evaluatedRevision"] = evaluated_revision;
  const bool transition = action == "trigger-transition";
  if (transition && !plan.already_applied)
  {
    if (const auto retry_after_ms = reserve_transition(command_state.transition_duration_ms()))
    {
      auto busy = command_result(body, "conflict", "transition is already in progress");
      busy["error"] = "transition_busy";
      busy["retryAfterMs"] = *retry_after_ms;
      return busy;
    }
    plan.completion_data["_baselineRevision"] = evaluated_revision;
  }
  const auto operation_id = std::to_string(monotonic_ns());

  auto accepted = command_result(body, "accepted");
  accepted["operationId"] = operation_id;
  accepted["acceptedAtMonotonicNs"] = monotonic_ns();
  accepted["evaluatedRevision"] = evaluated_revision;
  accepted["resultingRevision"] = plan.already_applied ? json(evaluated_revision) : json(nullptr);
  accepted["applied"] = !plan.already_applied;
  accepted["alreadyApplied"] = plan.already_applied;
  auto insertion = cache_accepted_response(command_key, std::move(accepted));
  if (!insertion.inserted)
  {
    if (transition && !plan.already_applied)
      release_transition();
    return insertion.response;
  }

  if (plan.already_applied)
  {
    audit_.command(device.id, action, plan.audit_target, "command_already_applied", operation_id);
  }
  else
  {
    audit_.command(device.id, action, plan.audit_target, "accepted", operation_id);
  }

  if (!queue_operation(device, std::string(action), std::move(plan), operation_id))
  {
    if (transition)
      release_transition();
    audit_.command(device.id, action, "", "failed", operation_id);
    emit_failure(command_id, operation_id, "backend-unavailable", "control command queue is full");
  }

  return insertion.response;
}

void GatewayCommandProcessor::set_message_sink(MessageSink sink)
{
  std::lock_guard lock(message_sink_mutex_);
  message_sink_ = std::move(sink);
}

void GatewayCommandProcessor::stop()
{
  {
    // Serialize the predicate change with the worker's check-and-wait to avoid a lost wakeup.
    std::lock_guard lock(operation_mutex_);
    if (stopping_.exchange(true))
      return;
  }

  operation_cv_.notify_all();
  if (operation_thread_.joinable())
    operation_thread_.join();
}

json GatewayCommandProcessor::command_result(const json& body,
                                             std::string status,
                                             std::string reason) const
{
  const auto command_id = body.find("commandId");
  json result = {
      {"type", "commandResult"},
      {"commandId", command_id != body.end() && command_id->is_string() ? *command_id : json("")},
      {"status", std::move(status)},
  };

  if (!reason.empty())
  {
    result["reason"] = std::move(reason);
  }

  return result;
}

GatewayCommandProcessor::CommandFailure GatewayCommandProcessor::command_failure(
    const json& body, std::string reason, CommandFailureCode code) const
{
  return {command_result(body, "rejected", std::move(reason)), code};
}

GatewayCommandProcessor::PreparedCommand GatewayCommandProcessor::prepare_command(
    std::string_view action, const json& body, const CommandStateSnapshot& state)
{
  if (action == "select-scene")
  {
    return prepare_scene_selection(body, state);
  }
  if (action == "set-scene-item-visibility")
  {
    return prepare_scene_item_visibility(body, state);
  }
  if (action == "set-audio-mute")
    return prepare_audio_mute(body, state);
  if (action == "set-input-mute")
  {
    return prepare_input_mute(body, state);
  }
  if (action == "set-input-solo")
  {
    return prepare_input_solo(body, state);
  }
  if (action == "set-input-volume")
  {
    return prepare_input_volume(body, state);
  }
  if (action == "set-input-balance")
  {
    return prepare_input_balance(body, state);
  }
  if (action == "set-master-volume")
  {
    return prepare_master_volume(body, state);
  }
  if (action == "set-input-monitoring")
  {
    return prepare_input_monitoring(body, state);
  }
  if (action == "trigger-transition")
  {
    return prepare_transition(body, state);
  }

  return prepare_output_command(action, body, state);
}

GatewayCommandProcessor::PreparedCommand
GatewayCommandProcessor::prepare_input_volume(const json& body, const CommandStateSnapshot& state)
{
  const auto input_id = required_string(body, "inputId");
  const auto volume_db = required_obs_volume_db(body);
  if (!input_id || !volume_db)
    return command_failure(body, "valid input volume command required");
  const auto backend = obs_.backend_info().capabilities;
  if (!backend.audio_volume)
    return command_failure(body, "input volume is unavailable");
  if (*volume_db < backend.audio_volume_minimum_db || *volume_db > backend.audio_volume_maximum_db)
    return command_failure(body, "input volume is outside the production backend range");
  const auto input = state.input(*input_id);
  if (!input)
  {
    return command_failure(body, "audio input not found", CommandFailureCode::resource_stale);
  }

  const auto effective_volume_db = quantized_volume_db(*volume_db, backend);

  CommandPlan plan;
  plan.obs_request = "SetInputVolume";
  plan.obs_request_data = {{"inputVolumeDb", effective_volume_db}};
  if (input->backend_identifier_preferred && !input->backend_uuid.empty())
    plan.obs_request_data["inputUuid"] = input->backend_uuid;
  else
    plan.obs_request_data["inputName"] = input->name;
  plan.audit_target = input->id;
  plan.completion_data = body;
  plan.completion_data["volumeDb"] = effective_volume_db;
  plan.already_applied =
      std::abs(input->volume_db - effective_volume_db) <= volume_completion_tolerance_db;
  auto observation_data = plan.obs_request_data;
  observation_data.erase("inputVolumeDb");
  plan.control_expectations.push_back(number_expectation(input->id + ":volume",
                                                         "GetInputVolume",
                                                         std::move(observation_data),
                                                         "inputVolumeDb",
                                                         input->volume_db,
                                                         volume_completion_tolerance_db));
  return plan;
}

GatewayCommandProcessor::PreparedCommand
GatewayCommandProcessor::prepare_input_balance(const json& body, const CommandStateSnapshot& state)
{
  const auto input_id = required_string(body, "inputId");
  const auto balance = required_balance(body);
  if (!input_id || !balance)
    return command_failure(body, "valid input balance command required");
  if (!obs_.backend_info().capabilities.audio_input_balance)
    return command_failure(body, "input balance is unavailable");
  const auto input = state.input(*input_id);
  if (!input)
    return command_failure(body, "audio input not found", CommandFailureCode::resource_stale);
  if (!input->balance_available)
    return command_failure(body, "input balance is unavailable");

  CommandPlan plan;
  plan.obs_request = "SetInputBalance";
  plan.obs_request_data = {{"inputBalance", *balance}};
  if (input->backend_identifier_preferred && !input->backend_uuid.empty())
    plan.obs_request_data["inputUuid"] = input->backend_uuid;
  else
    plan.obs_request_data["inputName"] = input->name;
  plan.audit_target = input->id;
  plan.completion_data = body;
  plan.already_applied = std::abs(input->balance - *balance) <= 0.0001;
  auto observation_data = plan.obs_request_data;
  observation_data.erase("inputBalance");
  plan.control_expectations.push_back(number_expectation(input->id + ":balance",
                                                         "GetInputBalance",
                                                         std::move(observation_data),
                                                         "inputBalance",
                                                         input->balance,
                                                         0.0001));
  return plan;
}

GatewayCommandProcessor::PreparedCommand
GatewayCommandProcessor::prepare_master_volume(const json& body, const CommandStateSnapshot& state)
{
  const auto volume_db = required_obs_volume_db(body);
  const auto backend = obs_.backend_info().capabilities;
  if (!volume_db)
    return command_failure(body, "valid master volume command required");
  if (!backend.audio_master_volume)
    return command_failure(body, "master volume is unavailable");
  if (*volume_db < backend.audio_volume_minimum_db || *volume_db > backend.audio_volume_maximum_db)
    return command_failure(body, "master volume is outside the production backend range");
  const auto master = state.audio_master();
  if (!master)
    return command_failure(body, "master volume is unavailable");

  const auto effective_volume_db = quantized_volume_db(*volume_db, backend);

  CommandPlan plan;
  plan.obs_request = "SetMasterVolume";
  plan.obs_request_data = {{"masterVolumeDb", effective_volume_db}};
  plan.audit_target = "master";
  plan.completion_data = body;
  plan.completion_data["volumeDb"] = effective_volume_db;
  plan.already_applied =
      std::abs(master->volume_db - effective_volume_db) <= volume_completion_tolerance_db;
  plan.control_expectations.push_back(number_expectation("audio-master:volume",
                                                         "GetMasterVolume",
                                                         json::object(),
                                                         "masterVolumeDb",
                                                         master->volume_db,
                                                         volume_completion_tolerance_db));
  return plan;
}

GatewayCommandProcessor::PreparedCommand
GatewayCommandProcessor::prepare_input_monitoring(const json& body,
                                                  const CommandStateSnapshot& state)
{
  const auto input_id = required_string(body, "inputId");
  const auto mode_name = required_string(body, "monitoringMode");
  const auto mode = mode_name ? parse_audio_monitoring_mode(*mode_name) : std::nullopt;
  if (!input_id || !mode)
    return command_failure(body, "valid input monitoring command required");

  const auto input = state.input(*input_id);
  if (!input)
    return command_failure(body, "audio input not found", CommandFailureCode::resource_stale);
  if (!input->monitoring_mode_available || input->backend_uuid.empty())
    return command_failure(body, "audio input monitoring is unavailable");

  CommandPlan plan;
  plan.obs_request = "SetInputAudioMonitorType";
  plan.obs_request_data = {
      {"inputUuid", input->backend_uuid},
      {"monitorType", obs_audio_monitor_type(*mode)},
  };
  plan.audit_target = input->id;
  plan.completion_data = body;
  plan.already_applied = input->monitoring_mode == *mode;
  auto observation_data = plan.obs_request_data;
  observation_data.erase("monitorType");
  plan.control_expectations.push_back(
      string_expectation(input->id + ":monitoring",
                         "GetInputAudioMonitorType",
                         std::move(observation_data),
                         "monitorType",
                         std::string(obs_audio_monitor_type(input->monitoring_mode))));
  return plan;
}

GatewayCommandProcessor::PreparedCommand
GatewayCommandProcessor::prepare_scene_selection(const json& body,
                                                 const CommandStateSnapshot& state)
{
  const auto scene_id = required_string(body, "sceneId");
  if (!scene_id)
    return command_failure(body, "valid sceneId required");
  const auto target = body.value("target", state.studio_mode() ? "preview" : "program");
  if (target != "program" && target != "preview")
    return command_failure(body, "invalid composition target");
  if (target == "preview" && !state.studio_mode())
    return command_failure(body, "Preview is unavailable");
  if (target == "preview" && !obs_.backend_info().capabilities.preview)
    return command_failure(body, "Preview is unavailable");
  const auto scene = state.scene(*scene_id);
  if (!scene)
  {
    return command_failure(body, "scene not found", CommandFailureCode::resource_stale);
  }

  CommandPlan plan;
  plan.obs_request = target == "preview" ? "SetCurrentPreviewScene" : "SetCurrentProgramScene";
  plan.obs_request_data = scene->backend_identifier_preferred && !scene->backend_id.empty()
                              ? json{{"sceneUuid", scene->backend_id}}
                              : json{{"sceneName", scene->name}};
  plan.audit_target = scene->id;
  plan.completion_data = body;
  plan.already_applied = target == "preview" ? scene->is_preview : scene->is_program;
  const auto current_scene_id =
      target == "preview" ? state.preview_scene_id() : state.program_scene_id();
  if (const auto current_scene = state.scene(current_scene_id))
  {
    const bool use_backend_id =
        current_scene->backend_identifier_preferred && !current_scene->backend_id.empty();
    plan.control_expectations.push_back(string_expectation(
        "composition:" + target,
        target == "preview" ? "GetCurrentPreviewScene" : "GetCurrentProgramScene",
        json::object(),
        target == "preview"
            ? (use_backend_id ? "currentPreviewSceneUuid" : "currentPreviewSceneName")
            : (use_backend_id ? "currentProgramSceneUuid" : "currentProgramSceneName"),
        use_backend_id ? current_scene->backend_id : current_scene->name));
  }
  return plan;
}

GatewayCommandProcessor::PreparedCommand
GatewayCommandProcessor::prepare_scene_item_visibility(const json& body,
                                                       const CommandStateSnapshot& state)
{
  const auto scene_id = required_string(body, "sceneId");
  const auto scene_item_id = required_integer(body, "sceneItemId");
  const auto enabled = required_boolean(body, "enabled");
  if (!scene_id || !scene_item_id || !enabled)
    return command_failure(body, "valid scene item command required");
  const auto scene = state.scene(*scene_id);
  const auto item = state.item(*scene_id, *scene_item_id);

  if (!scene || !item)
  {
    return command_failure(body, "scene item not found", CommandFailureCode::resource_stale);
  }
  if (!item->visibility_controllable)
    return command_failure(body, "layer visibility is not controllable");
  if (!item->visibility_state_available)
    return command_failure(body, "layer visibility state is unavailable");

  const auto expected_resource_id = body.find("_expectedResourceId");
  if (expected_resource_id != body.end() &&
      (!expected_resource_id->is_string() ||
       expected_resource_id->get<std::string>() != item->source_id))
    return command_failure(body, "scene item resource changed", CommandFailureCode::resource_stale);

  const auto container_name = item->container_name.empty() ? scene->name : item->container_name;

  CommandPlan plan;
  plan.obs_request = "SetSceneItemEnabled";
  plan.obs_request_data = {
      {"sceneName", container_name},
      {"sceneItemId", item->id},
      {"sceneItemEnabled", *enabled},
  };
  if (scene->backend_identifier_preferred && !scene->backend_id.empty())
    plan.obs_request_data["sceneUuid"] = scene->backend_id;
  plan.audit_target = scene->id + ":" + std::to_string(item->id);
  plan.completion_data = body;
  plan.completion_data.erase("_expectedResourceId");
  plan.resource_identity = item->source_id;
  plan.already_applied = item->enabled == *enabled;
  auto observation_data = plan.obs_request_data;
  observation_data.erase("sceneItemEnabled");
  plan.control_expectations.push_back(boolean_expectation(plan.audit_target + ":visibility",
                                                          "GetSceneItemEnabled",
                                                          std::move(observation_data),
                                                          "sceneItemEnabled",
                                                          item->enabled));
  return plan;
}

std::string_view GatewayCommandProcessor::failure_reason(CommandFailureCode code)
{
  switch (code)
  {
  case CommandFailureCode::resource_stale:
    return "resource-stale";
  case CommandFailureCode::audio_track_read_only:
    return "audio-track-read-only";
  default:
    return "invalid-command-state";
  }
}

GatewayCommandProcessor::PreparedCommand
GatewayCommandProcessor::prepare_audio_mute(const json& body, const CommandStateSnapshot& state)
{
  const auto track = required_string(body, "trackId");
  const auto muted = required_boolean(body, "muted");
  if (!track || track->size() > 256 || !muted)
    return command_failure(body, "valid audio track mute command required");
  const auto backend = obs_.backend_info().capabilities;
  if (*track == "audio-monitor" || (*track == "audio-master" && !backend.audio_master_mute))
    return command_failure(
        body, "audio track is read-only", CommandFailureCode::audio_track_read_only);
  if (track->starts_with("input:"))
  {
    if (!backend.audio_mute)
      return command_failure(
          body, "audio track is read-only", CommandFailureCode::audio_track_read_only);
    auto input_body = body;
    input_body["inputId"] = track->substr(6);
    return prepare_input_mute(input_body, state);
  }
  if (*track != "audio-master")
    return command_failure(body, "audio track not found", CommandFailureCode::resource_stale);
  const auto master = state.audio_master();
  if (!master)
    return command_failure(body, "Master state is unavailable", CommandFailureCode::resource_stale);

  CommandPlan plan;
  plan.obs_request = "SetMasterMute";
  plan.obs_request_data = {{"masterMuted", *muted}};
  plan.audit_target = *track;
  plan.completion_data = body;
  plan.already_applied = master->muted == *muted;
  plan.control_expectations.push_back(boolean_expectation(
      "audio-master:mute", "GetMasterMute", json::object(), "masterMuted", master->muted));
  plan.control_expectations.push_back(number_expectation("audio-master:volume",
                                                         "GetMasterVolume",
                                                         json::object(),
                                                         "masterVolumeDb",
                                                         master->volume_db,
                                                         volume_completion_tolerance_db));
  return plan;
}

GatewayCommandProcessor::PreparedCommand
GatewayCommandProcessor::prepare_input_mute(const json& body, const CommandStateSnapshot& state)
{
  const auto input_id = required_string(body, "inputId");
  const auto muted = required_boolean(body, "muted");
  if (!input_id || !muted)
    return command_failure(body, "valid input mute command required");
  const auto input = state.input(*input_id);
  if (!obs_.backend_info().capabilities.audio_mute)
    return command_failure(body, "audio input mute is unavailable");
  if (!input)
  {
    return command_failure(body, "audio input not found", CommandFailureCode::resource_stale);
  }

  CommandPlan plan;
  plan.obs_request = "SetInputMute";
  plan.obs_request_data = {{"inputMuted", *muted}};
  if (input->backend_identifier_preferred && !input->backend_uuid.empty())
    plan.obs_request_data["inputUuid"] = input->backend_uuid;
  else
    plan.obs_request_data["inputName"] = input->name;
  plan.audit_target = input->id;
  plan.completion_data = body;
  plan.already_applied = input->muted == *muted;
  auto observation_data = plan.obs_request_data;
  observation_data.erase("inputMuted");
  plan.control_expectations.push_back(boolean_expectation(input->id + ":mute",
                                                          "GetInputMute",
                                                          std::move(observation_data),
                                                          "inputMuted",
                                                          input->muted));
  return plan;
}

GatewayCommandProcessor::PreparedCommand
GatewayCommandProcessor::prepare_input_solo(const json& body, const CommandStateSnapshot& state)
{
  const auto input_id = required_string(body, "inputId");
  const auto solo = required_boolean(body, "solo");
  if (!input_id || !solo)
    return command_failure(body, "valid input solo command required");
  if (!obs_.backend_info().capabilities.audio_solo)
    return command_failure(body, "audio input solo is unsupported");
  const auto input = state.input(*input_id);
  if (!input)
  {
    return command_failure(body, "audio input not found", CommandFailureCode::resource_stale);
  }

  CommandPlan plan;
  plan.obs_request = "SetInputSolo";
  plan.obs_request_data = {{"inputSolo", *solo}};
  if (input->backend_identifier_preferred && !input->backend_uuid.empty())
    plan.obs_request_data["inputUuid"] = input->backend_uuid;
  else
    plan.obs_request_data["inputName"] = input->name;
  plan.audit_target = input->id;
  plan.completion_data = body;
  plan.already_applied = input->solo == *solo && (!*solo || state.solo_input_count() == 1);
  auto observation_data = plan.obs_request_data;
  observation_data.erase("inputSolo");
  plan.control_expectations.push_back(boolean_expectation(
      input->id + ":solo", "GetInputSolo", std::move(observation_data), "inputSolo", input->solo));
  return plan;
}

GatewayCommandProcessor::PreparedCommand
GatewayCommandProcessor::prepare_transition(const json& body, const CommandStateSnapshot& state)
{
  if (!obs_.backend_info().capabilities.transition_execute)
    return command_failure(body, "transition execution is unavailable");
  if (!state.studio_mode())
  {
    return command_failure(body, "Studio Mode is disabled");
  }

  CommandPlan plan;
  auto completion_data = body;
  completion_data["_baselineProgramFrameNs"] = state.program_frame_ns();
  completion_data["_baselineProgramSceneId"] = state.program_scene_id();
  completion_data["_transitionCompletionRequiresVideo"] =
      state.transition_completion_requires_video();

  plan.obs_request = "TriggerStudioModeTransition";
  plan.completion_data = std::move(completion_data);
  plan.control_expectations.push_back(boolean_expectation("studio-mode",
                                                          "GetStudioModeEnabled",
                                                          json::object(),
                                                          "studioModeEnabled",
                                                          state.studio_mode()));
  if (const auto program = state.scene(state.program_scene_id()))
  {
    const bool use_backend_id =
        program->backend_identifier_preferred && !program->backend_id.empty();
    plan.control_expectations.push_back(
        string_expectation("composition:program",
                           "GetCurrentProgramScene",
                           json::object(),
                           use_backend_id ? "currentProgramSceneUuid" : "currentProgramSceneName",
                           use_backend_id ? program->backend_id : program->name));
  }
  if (const auto preview = state.scene(state.preview_scene_id()))
  {
    const bool use_backend_id =
        preview->backend_identifier_preferred && !preview->backend_id.empty();
    plan.control_expectations.push_back(
        string_expectation("composition:preview",
                           "GetCurrentPreviewScene",
                           json::object(),
                           use_backend_id ? "currentPreviewSceneUuid" : "currentPreviewSceneName",
                           use_backend_id ? preview->backend_id : preview->name));
  }
  return plan;
}

GatewayCommandProcessor::PreparedCommand GatewayCommandProcessor::prepare_output_command(
    std::string_view action, const json& body, const CommandStateSnapshot& state)
{
  const auto request = output_requests.find(std::string(action));
  if (request == output_requests.end())
  {
    return command_failure(body, "unknown command");
  }

  const bool stream_active = state.stream_active();
  const bool stream_configured = state.stream_configured();
  const bool record_active = state.record_active();
  const bool record_paused = state.record_paused();

  const auto capabilities = obs_.backend_info().capabilities;
  if ((action == "start-stream" || action == "stop-stream") && !capabilities.streaming_start_stop)
    return command_failure(body, "stream output control is unavailable");
  if ((action == "start-record" || action == "stop-record") && !capabilities.recording_start_stop)
    return command_failure(body, "record output control is unavailable");
  if ((action == "pause-record" || action == "resume-record") &&
      (!capabilities.recording_pause || !obs_.recording_pause_available()))
    return command_failure(body, "recording pause and resume are unavailable");

  if (action == "start-stream" && !stream_active && !stream_configured &&
      capabilities.stream_configuration_known)
  {
    return command_failure(body, "stream destination is not configured");
  }
  if (action == "pause-record" && !record_active)
  {
    return command_failure(body, "recording cannot be paused in its current state");
  }
  if (action == "resume-record" && !record_active)
  {
    return command_failure(body, "recording cannot be resumed in its current state");
  }

  CommandPlan plan;
  plan.obs_request = request->second;
  plan.audit_target = std::string(action);
  plan.completion_data = body;
  if (action == "start-stream")
    plan.already_applied = stream_active;
  else if (action == "stop-stream")
    plan.already_applied = !stream_active;
  else if (action == "start-record")
    plan.already_applied = record_active;
  else if (action == "stop-record")
    plan.already_applied = !record_active;
  else if (action == "pause-record")
    plan.already_applied = record_paused;
  else if (action == "resume-record")
    plan.already_applied = !record_paused;
  if (action == "start-stream" || action == "stop-stream")
  {
    if (!obs_.backend_info().capabilities.explicit_start_stop_for_stream)
    {
      plan.control_expectations.push_back(boolean_expectation(
          "output:stream", "GetStreamStatus", json::object(), "outputActive", stream_active));
    }
  }
  else if ((action != "start-record" && action != "stop-record") ||
           !obs_.backend_info().capabilities.explicit_start_stop_for_record)
  {
    plan.control_expectations.push_back(boolean_expectation(
        "output:record:active", "GetRecordStatus", json::object(), "outputActive", record_active));
    plan.control_expectations.push_back(boolean_expectation(
        "output:record:paused", "GetRecordStatus", json::object(), "outputPaused", record_paused));
  }
  return plan;
}

bool GatewayCommandProcessor::queue_operation(Device device,
                                              std::string action,
                                              CommandPlan plan,
                                              std::string operation_id)
{
  {
    std::lock_guard lock(operation_mutex_);
    if (stopping_ || operations_.size() >= operation_queue_capacity)
      return false;
    operations_.push_back(QueuedOperation{
        std::move(device), std::move(action), std::move(plan), std::move(operation_id)});
  }
  operation_cv_.notify_one();
  return true;
}

bool GatewayCommandProcessor::authorized(const Device& device) const
{
  return has_current_control_authority(audit_, device);
}

void GatewayCommandProcessor::operation_loop()
{
  while (true)
  {
    std::variant<QueuedOperation, QueuedCollectionSelection> operation;
    {
      std::unique_lock lock(operation_mutex_);
      operation_cv_.wait(lock, [this] { return stopping_ || !operations_.empty(); });
      if (operations_.empty())
        return;
      operation = std::move(operations_.front());
      operations_.pop_front();
    }

    if (const auto* collection = std::get_if<QueuedCollectionSelection>(&operation))
      run_collection_selection(*collection);
    else
    {
      const auto& command = std::get<QueuedOperation>(operation);
      run_operation(command.device, command.action, command.plan, command.operation_id);
      if (command.action == "trigger-transition")
        release_transition();
    }
  }
}

void GatewayCommandProcessor::run_operation(const Device& device,
                                            const std::string& action,
                                            const CommandPlan& plan,
                                            const std::string& operation_id)
{
  try
  {
    const auto admit_execution = [&]
    {
      if (!stopping_ && authorized(device))
        return true;
      audit_.command(device.id, action, plan.audit_target, "rejected", operation_id);
      emit_failure(plan.completion_data.value("commandId", ""),
                   operation_id,
                   stopping_ ? "backend-unavailable" : "permission-required",
                   stopping_ ? "command worker is stopping" : "command authority changed");
      return false;
    };
    if (!admit_execution())
      return;
    const auto current_state = state_.command_state_snapshot();
    if (!current_state.fresh())
    {
      audit_.command(device.id,
                     action,
                     plan.audit_target,
                     "command_semantic_precondition_failed",
                     operation_id);
      emit_failure(plan.completion_data.value("commandId", ""),
                   operation_id,
                   "backend-unavailable",
                   "OBS state is stale");
      return;
    }
    auto revalidated = prepare_command(action, plan.completion_data, current_state);
    if (auto* failure = std::get_if<CommandFailure>(&revalidated))
    {
      const auto reason = failure->response.value("reason", "semantic precondition failed");
      audit_.command(device.id,
                     action,
                     plan.audit_target,
                     "command_semantic_precondition_failed",
                     operation_id);
      emit_failure(plan.completion_data.value("commandId", ""),
                   operation_id,
                   failure_reason(failure->code),
                   reason);
      return;
    }

    auto current_plan = std::get<CommandPlan>(std::move(revalidated));
    if (plan.resource_identity && current_plan.resource_identity != plan.resource_identity)
    {
      audit_.command(device.id,
                     action,
                     plan.audit_target,
                     "command_semantic_precondition_failed",
                     operation_id);
      emit_failure(plan.completion_data.value("commandId", ""),
                   operation_id,
                   "resource-stale",
                   "scene item resource changed");
      return;
    }
    if (current_plan.already_applied)
    {
      audit_.command(
          device.id, action, current_plan.audit_target, "command_already_applied", operation_id);
      emit({{"type", "commandCompleted"},
            {"commandId", plan.completion_data.value("commandId", "")},
            {"operationId", operation_id},
            {"status", "completed"},
            {"evaluatedRevision", current_state.revision()},
            {"resultingRevision", current_state.revision()},
            {"applied", false},
            {"alreadyApplied", true},
            {"sceneState", state_.snapshot()["sceneState"]}});
      return;
    }

    const auto execution_verification = verify_control_state(current_plan);
    if (execution_verification != ControlStateVerification::matched)
    {
      request_reconcile_();
      audit_.command(device.id,
                     action,
                     current_plan.audit_target,
                     "command_authoritative_state_rejected",
                     operation_id);
      emit_failure(plan.completion_data.value("commandId", ""),
                   operation_id,
                   execution_verification == ControlStateVerification::changed
                       ? "invalid-command-state"
                       : "backend-unavailable",
                   execution_verification == ControlStateVerification::changed
                       ? "production state changed before command execution"
                       : "production state could not be verified before command execution");
      return;
    }

    // Backend verification can block. A credential change during that wait must take effect
    // before a native operation is issued, including a revoke followed by a new pairing.
    if (!admit_execution())
      return;
    current_plan.completion_data["_baselineRevision"] = current_state.revision();
    if (!obs_.request_authorized(current_plan.obs_request,
                                 current_plan.obs_request_data,
                                 [&] { return !stopping_ && authorized(device); }))
    {
      audit_.command(device.id, action, plan.audit_target, "rejected", operation_id);
      emit_failure(plan.completion_data.value("commandId", ""),
                   operation_id,
                   stopping_ ? "backend-unavailable" : "permission-required",
                   "command authority changed before provider dispatch");
      return;
    }

    const auto deadline = Clock::now() + command_completion_timeout;
    while (!stopping_ && Clock::now() < deadline)
    {
      if (is_completed(action, current_plan.completion_data))
      {
        audit_.command(device.id, action, current_plan.audit_target, "completed", operation_id);
        emit({
            {"type", "commandCompleted"},
            {"commandId", plan.completion_data.value("commandId", "")},
            {"operationId", operation_id},
            {"status", "completed"},
            {"evaluatedRevision", current_state.revision()},
            {"resultingRevision", state_.revision()},
            {"applied", true},
            {"alreadyApplied", false},
            {"sceneState", state_.snapshot()["sceneState"]},
        });
        return;
      }

      std::this_thread::sleep_for(completion_poll_interval);
    }

    request_reconcile_();
    throw std::runtime_error("authoritative OBS event timed out");
  }
  catch (const std::exception& error)
  {
    audit_.command(device.id, action, plan.audit_target, "failed", operation_id);
    emit_failure(plan.completion_data.value("commandId", ""),
                 operation_id,
                 "backend-unavailable",
                 std::string(error.what()).substr(0, 240));
  }
}

ControlStateVerification GatewayCommandProcessor::verify_control_state(const CommandPlan& plan)
{
  if (!obs_.backend_info().capabilities.authoritative_control_verification)
    return ControlStateVerification::matched;

  for (const auto& expectation : plan.control_expectations)
  {
    try
    {
      const auto response = obs_.request_current(expectation.request_type,
                                                 expectation.request_data,
                                                 std::source_location::current(),
                                                 "authoritative control-state guard");
      const auto verification = verify_control_response(expectation, response);
      if (verification != ControlStateVerification::matched)
        return verification;
    }
    catch (const std::exception&)
    {
      return ControlStateVerification::unavailable;
    }
  }
  return ControlStateVerification::matched;
}

void GatewayCommandProcessor::emit_failure(std::string_view command_id,
                                           std::string_view operation_id,
                                           std::string_view reason_code,
                                           std::string reason)
{
  emit({{"type", "commandFailed"},
        {"commandId", command_id},
        {"operationId", operation_id},
        {"status", "failed"},
        {"reasonCode", reason_code},
        {"reason", std::move(reason)}});
}

bool GatewayCommandProcessor::is_completed(std::string_view action,
                                           const json& completion_data) const
{
  if (action == "select-scene")
  {
    const auto scene = state_.scene(completion_data.value("sceneId", ""));
    if (!scene)
    {
      return false;
    }

    const auto target =
        completion_data.value("target", state_.studio_mode() ? "preview" : "program");
    return target == "preview" ? scene->is_preview : scene->is_program;
  }

  if (action == "set-scene-item-visibility")
  {
    const auto item = state_.item(completion_data.value("sceneId", ""),
                                  completion_data.value("sceneItemId", 0LL));
    return item && item->enabled == completion_data.value("enabled", false);
  }

  if (action == "set-audio-mute" && completion_data.value("trackId", "") == "audio-master")
  {
    const auto master = state_.command_state_snapshot().audio_master();
    return master && master->muted == completion_data.value("muted", false);
  }
  if (action == "set-input-mute" || action == "set-audio-mute")
  {
    const auto input = state_.input(completion_data.value("inputId", ""));
    return input && input->muted == completion_data.value("muted", false);
  }

  if (action == "set-input-solo")
  {
    const auto input = state_.input(completion_data.value("inputId", ""));
    const bool solo = completion_data.value("solo", false);
    return input && input->solo == solo && (!solo || state_.solo_input_count() == 1);
  }

  if (action == "set-input-volume")
  {
    const auto input = state_.input(completion_data.value("inputId", ""));
    return input && std::abs(input->volume_db - completion_data.value("volumeDb", 0.0)) <=
                        volume_completion_tolerance_db;
  }

  if (action == "set-input-balance")
  {
    const auto input = state_.input(completion_data.value("inputId", ""));
    return input && std::abs(input->balance - completion_data.value("balance", 0.0)) <= 0.0001;
  }

  if (action == "set-master-volume")
  {
    const auto master = state_.command_state_snapshot().audio_master();
    return master && std::abs(master->volume_db - completion_data.value("volumeDb", 0.0)) <=
                         volume_completion_tolerance_db;
  }

  if (action == "set-input-monitoring")
  {
    const auto input = state_.input(completion_data.value("inputId", ""));
    const auto mode =
        parse_audio_monitoring_mode(completion_data.value("monitoringMode", std::string{}));
    return input && input->monitoring_mode_available && mode && input->monitoring_mode == *mode;
  }

  const auto snapshot = state_.snapshot();
  const auto& outputs = snapshot["outputs"];

  if (action == "start-stream")
  {
    return outputs["stream"].value("active", false);
  }
  if (action == "stop-stream")
  {
    return !outputs["stream"].value("active", false);
  }
  if (action == "start-record")
  {
    return outputs["record"].value("active", false);
  }
  if (action == "stop-record")
  {
    return !outputs["record"].value("active", false);
  }
  if (action == "pause-record")
  {
    return outputs["record"].value("paused", false);
  }
  if (action == "resume-record")
  {
    return !outputs["record"].value("paused", false);
  }
  if (action == "trigger-transition")
  {
    const auto baseline_scene = completion_data.value("_baselineProgramSceneId", "");
    if (!completion_data.value("_transitionCompletionRequiresVideo", true))
      return state_.command_state_snapshot().program_scene_id() != baseline_scene &&
             state_.revision() > completion_data.value("_baselineRevision", 0ULL);
    const auto current_frame = snapshot["video"]["program"].value("observedFrameNs", 0ULL);
    const auto baseline_frame = completion_data.value("_baselineProgramFrameNs", 0ULL);
    return current_frame > baseline_frame &&
           state_.revision() > completion_data.value("_baselineRevision", 0ULL);
  }

  return false;
}

std::optional<json> GatewayCommandProcessor::cached_response(const std::string& key)
{
  std::lock_guard lock(command_cache_mutex_);
  remove_expired_cache_entries();

  const auto found = command_cache_.find(key);
  if (found == command_cache_.end())
  {
    return std::nullopt;
  }

  return found->second.second;
}

GatewayCommandProcessor::CacheInsertion
GatewayCommandProcessor::cache_accepted_response(const std::string& key, json response)
{
  std::lock_guard lock(command_cache_mutex_);
  remove_expired_cache_entries();
  if (const auto existing = command_cache_.find(key); existing != command_cache_.end())
    return {existing->second.second, false};
  const auto response_bytes = bounded_json_size(response, 8192);
  const auto bytes = response_bytes.value_or(8192) + key.size() * 2 + 256;
  if (key.size() > 512 || !response_bytes || command_cache_.size() >= 16'384 ||
      bytes > 32 * 1024 * 1024 - receipt_bytes_)
  {
    return {{{"status", "rejected"},
             {"commandId", response.value("commandId", "")},
             {"reasonCode", "command-capacity-exceeded"},
             {"reason", "command receipt capacity reached"}},
            false};
  }
  const auto [entry, inserted] =
      command_cache_.emplace(key, std::make_pair(receipt_clock_(), response));

  if (!inserted)
  {
    return {entry->second.second, false};
  }

  try
  {
    receipt_expiry_.push_back({key, bytes});
  }
  catch (...)
  {
    command_cache_.erase(entry);
    throw;
  }
  receipt_bytes_ += bytes;
  return {std::move(response), true};
}

void GatewayCommandProcessor::remove_expired_cache_entries()
{
  const auto now = receipt_clock_();
  while (!receipt_expiry_.empty())
  {
    const auto entry = command_cache_.find(receipt_expiry_.front().key);
    if (now - entry->second.first <= command_cache_lifetime)
      break;
    receipt_bytes_ -= receipt_expiry_.front().bytes;
    command_cache_.erase(entry);
    receipt_expiry_.pop_front();
  }
}

std::optional<std::uint32_t>
GatewayCommandProcessor::reserve_transition(std::uint32_t transition_duration_ms)
{
  std::lock_guard lock(transition_mutex_);
  const auto now = Clock::now();
  if (reserved_transitions_ >= 2)
  {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(active_transition_deadline_, now + std::chrono::milliseconds(50)) - now);
    return static_cast<std::uint32_t>(remaining.count());
  }
  if (reserved_transitions_ == 0)
    active_transition_deadline_ = now + std::chrono::milliseconds(transition_duration_ms);
  ++reserved_transitions_;
  return std::nullopt;
}

void GatewayCommandProcessor::release_transition()
{
  std::lock_guard lock(transition_mutex_);
  if (reserved_transitions_ > 0)
    --reserved_transitions_;
  if (reserved_transitions_ > 0)
    active_transition_deadline_ =
        Clock::now() + std::chrono::milliseconds(state_.transition_duration_ms());
}

void GatewayCommandProcessor::emit(json message)
{
  MessageSink sink;
  {
    std::lock_guard lock(message_sink_mutex_);
    sink = message_sink_;
  }

  if (sink)
  {
    sink(message);
  }
}

} // namespace studio_remote
