#include "critical_live_control_service.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <stdexcept>
#include <utility>

namespace studio_remote
{
namespace
{
constexpr std::size_t queue_capacity = 16;
constexpr std::size_t terminal_history_capacity = 128;
constexpr auto obs_response_timeout = std::chrono::milliseconds(1000);
constexpr auto output_convergence_timeout = std::chrono::milliseconds(3000);
constexpr auto poll_interval = std::chrono::milliseconds(25);

std::optional<std::string> required_string(const json& body, std::string_view field)
{
  const auto found = body.find(std::string(field));
  if (found == body.end() || !found->is_string())
    return std::nullopt;
  auto value = found->get<std::string>();
  return value.empty() ? std::nullopt : std::optional<std::string>(std::move(value));
}

std::optional<std::uint64_t> required_sequence(const json& body)
{
  const auto found = body.find("clientSequence");
  if (found == body.end() || !found->is_number_integer())
    return std::nullopt;
  if (found->is_number_unsigned())
    return found->get<std::uint64_t>();
  const auto value = found->get<std::int64_t>();
  return value >= 0 ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(value))
                    : std::nullopt;
}

std::string public_status(CriticalActionStatus status)
{
  std::string value(critical_action_status_name(status));
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return value;
}
} // namespace

std::string_view critical_action_wire_name(CriticalActionType type) noexcept
{
  switch (type)
  {
  case CriticalActionType::record_start:
    return "RECORD_START";
  case CriticalActionType::record_pause:
    return "RECORD_PAUSE";
  case CriticalActionType::record_resume:
    return "RECORD_RESUME";
  case CriticalActionType::record_stop:
    return "RECORD_STOP";
  case CriticalActionType::stream_start:
    return "STREAM_START";
  case CriticalActionType::stream_stop:
    return "STREAM_STOP";
  case CriticalActionType::stream_destination_start:
    return "STREAM_DESTINATION_START";
  case CriticalActionType::stream_destination_stop:
    return "STREAM_DESTINATION_STOP";
  case CriticalActionType::transition_trigger:
    return "TRANSITION_TRIGGER";
  }
  return "RECORD_START";
}

std::string_view critical_action_status_name(CriticalActionStatus status) noexcept
{
  switch (status)
  {
  case CriticalActionStatus::accepted:
    return "accepted";
  case CriticalActionStatus::executing:
    return "executing";
  case CriticalActionStatus::verifying:
    return "verifying";
  case CriticalActionStatus::completed:
    return "completed";
  case CriticalActionStatus::rejected:
    return "rejected";
  case CriticalActionStatus::faulted:
    return "faulted";
  case CriticalActionStatus::canceled:
    return "canceled";
  }
  return "rejected";
}

std::optional<CriticalActionType> parse_critical_action(std::string_view value)
{
  if (value == "RECORD_START")
    return CriticalActionType::record_start;
  if (value == "RECORD_PAUSE")
    return CriticalActionType::record_pause;
  if (value == "RECORD_RESUME")
    return CriticalActionType::record_resume;
  if (value == "RECORD_STOP")
    return CriticalActionType::record_stop;
  if (value == "STREAM_START")
    return CriticalActionType::stream_start;
  if (value == "STREAM_STOP")
    return CriticalActionType::stream_stop;
  if (value == "STREAM_DESTINATION_START")
    return CriticalActionType::stream_destination_start;
  if (value == "STREAM_DESTINATION_STOP")
    return CriticalActionType::stream_destination_stop;
  if (value == "TRANSITION_TRIGGER")
    return CriticalActionType::transition_trigger;
  return std::nullopt;
}

std::vector<std::string> critical_action_capability_names(bool recording_pause_available)
{
  std::vector<std::string> actions = {"RECORD_START"};
  if (recording_pause_available)
  {
    actions.push_back("RECORD_PAUSE");
    actions.push_back("RECORD_RESUME");
  }
  actions.insert(actions.end(),
                 {"RECORD_STOP", "STREAM_START", "STREAM_STOP", "TRANSITION_TRIGGER"});
  return actions;
}

std::vector<std::string>
critical_action_capability_names(const ProductionBackendCapabilities& capabilities)
{
  std::vector<std::string> actions;
  if (capabilities.recording_start_stop)
  {
    actions.push_back("RECORD_START");
    if (capabilities.recording_pause)
    {
      actions.push_back("RECORD_PAUSE");
      actions.push_back("RECORD_RESUME");
    }
    actions.push_back("RECORD_STOP");
  }
  if (capabilities.streaming_start_stop)
  {
    actions.push_back("STREAM_START");
    actions.push_back("STREAM_STOP");
    if (capabilities.streaming_destinations &&
        capabilities.streaming_destination_individual_control)
    {
      actions.push_back("STREAM_DESTINATION_START");
      actions.push_back("STREAM_DESTINATION_STOP");
    }
  }
  if (capabilities.transition_execute)
    actions.push_back("TRANSITION_TRIGGER");
  return actions;
}

CriticalLiveControlService::CriticalLiveControlService(IObsAdapter& obs,
                                                       StateStore& state,
                                                       AuditStore& audit,
                                                       IObsRequestExecutor* request_executor,
                                                       ReconcileRequest request_reconcile)
    : obs_(obs), request_executor_(request_executor), state_(state), audit_(audit),
      request_reconcile_(std::move(request_reconcile))
{
}

CriticalLiveControlService::~CriticalLiveControlService()
{
  stop();
}

void CriticalLiveControlService::start()
{
  if (started_.exchange(true))
    return;
  stopping_ = false;
  worker_ = std::thread([this] { worker_loop(); });
}

void CriticalLiveControlService::stop()
{
  if (!started_.exchange(false))
    return;
  stopping_ = true;
  {
    std::lock_guard lock(mutex_);
    for (auto& action : queue_)
    {
      action.status = CriticalActionStatus::canceled;
      action.reason = "expired-companion-stopped";
      known_by_action_id_.insert_or_assign(action.action_id,
                                           KnownAction{action,
                                                       payload_signature(action.device,
                                                                         action.client_sequence,
                                                                         action.type,
                                                                         action.transition_id,
                                                                         action.destination_id)});
    }
    queue_.clear();
  }
  changed_.notify_all();
  if (worker_.joinable())
    worker_.join();
}

bool CriticalLiveControlService::available() const noexcept
{
  return started_.load();
}

void CriticalLiveControlService::set_message_sink(MessageSink sink)
{
  std::lock_guard lock(message_sink_mutex_);
  message_sink_ = std::move(sink);
}

CriticalSubmitResult CriticalLiveControlService::submit(const Device& device, const json& body)
{
  const auto action_id = required_string(body, "actionId");
  if (!action_id || action_id->size() > 128)
    return reject("", 422, "invalid-command-state", "valid actionId required");
  const auto client_sequence = required_sequence(body);
  if (!client_sequence)
    return reject(*action_id, 422, "invalid-command-state", "valid clientSequence required");
  const auto action_type = parse_critical_action(body.value("action", ""));
  if (!action_type)
    return reject(*action_id, 422, "invalid-command-state", "unknown critical action");
  std::string transition_id;
  std::string destination_id;
  if (*action_type == CriticalActionType::transition_trigger)
  {
    const auto selected_transition = body.find("transitionId");
    if (selected_transition != body.end())
    {
      if (!selected_transition->is_string() || selected_transition->get<std::string>().empty() ||
          selected_transition->get<std::string>().size() > 128)
        return reject(*action_id, 422, "invalid-command-state", "invalid transitionId");
      transition_id = selected_transition->get<std::string>();
    }
    const auto legacy_kind = body.find("transitionKind");
    if (legacy_kind != body.end() &&
        (!legacy_kind->is_string() ||
         legacy_kind->get_ref<const std::string&>() != "cut-or-configured"))
      return reject(*action_id, 422, "invalid-command-state", "invalid transitionKind");
  }
  if (*action_type == CriticalActionType::stream_destination_start ||
      *action_type == CriticalActionType::stream_destination_stop)
  {
    const auto selected_destination = body.find("destinationId");
    if (selected_destination == body.end() || !selected_destination->is_string() ||
        selected_destination->get<std::string>().empty() ||
        selected_destination->get<std::string>().size() > 128)
      return reject(*action_id, 422, "invalid-command-state", "invalid destinationId");
    destination_id = selected_destination->get<std::string>();
  }
  if (const auto supplied_device_id = body.find("deviceId");
      supplied_device_id != body.end() &&
      (!supplied_device_id->is_string() || supplied_device_id->get<std::string>() != device.id))
    return reject(*action_id, 403, "unauthorized", "deviceId does not match token");
  if (device.role < Role::operator_)
    return reject(*action_id, 403, "unauthorized", "operator role required");
  if ((*action_type == CriticalActionType::record_pause ||
       *action_type == CriticalActionType::record_resume) &&
      !obs_.backend_info().capabilities.recording_pause)
  {
    return reject(*action_id,
                  409,
                  "capability-unavailable",
                  "The selected production backend does not support recording pause and resume");
  }
  if ((*action_type == CriticalActionType::record_pause ||
       *action_type == CriticalActionType::record_resume) &&
      !obs_.recording_pause_available())
  {
    return reject(*action_id,
                  409,
                  "capability-unavailable",
                  "OBS has disabled recording pause and resume for the current output");
  }
  const auto backend_capabilities = obs_.backend_info().capabilities;
  if ((*action_type == CriticalActionType::record_start ||
       *action_type == CriticalActionType::record_stop) &&
      !backend_capabilities.recording_start_stop)
    return reject(*action_id,
                  409,
                  "capability-unavailable",
                  "The selected production backend does not support recording control");
  if ((*action_type == CriticalActionType::stream_start ||
       *action_type == CriticalActionType::stream_stop) &&
      !backend_capabilities.streaming_start_stop)
    return reject(*action_id,
                  409,
                  "capability-unavailable",
                  "The selected production backend does not support streaming control");
  if ((*action_type == CriticalActionType::stream_destination_start ||
       *action_type == CriticalActionType::stream_destination_stop) &&
      (!backend_capabilities.streaming_start_stop || !backend_capabilities.streaming_destinations ||
       !backend_capabilities.streaming_destination_individual_control))
    return reject(
        *action_id,
        409,
        "capability-unavailable",
        "The selected production backend does not support individual stream destination control");
  if (*action_type == CriticalActionType::transition_trigger &&
      !backend_capabilities.transition_execute)
    return reject(*action_id,
                  409,
                  "capability-unavailable",
                  "The selected production backend does not support transition execution");
  if (!started_)
    return reject(
        *action_id, 503, "critical-controls-unavailable", "critical controls are stopped");

  const auto signature =
      payload_signature(device, *client_sequence, *action_type, transition_id, destination_id);
  {
    std::unique_lock lock(mutex_);
    if (const auto found = known_by_action_id_.find(*action_id); found != known_by_action_id_.end())
    {
      if (found->second.payload_signature != signature)
      {
        return block_admission(lock,
                               device,
                               *action_id,
                               "protocol-violation",
                               "Duplicate critical action ID was reused with a different payload.");
      }
      return {found->second.action.status == CriticalActionStatus::completed ? 200 : 202,
              action_json(found->second.action)};
    }
  }

  {
    std::lock_guard lock(mutex_);
    if (configuration_reserved_)
      return reject(*action_id, 409, "collection-load-in-progress");
  }

  const auto admission_snapshot = state_.command_state_snapshot();
  const auto admission_verification = verify_control_state(*action_type, admission_snapshot);
  if (admission_verification != ControlStateVerification::matched)
  {
    if (request_reconcile_)
      request_reconcile_();
    return reject(*action_id,
                  admission_verification == ControlStateVerification::changed ? 409 : 503,
                  admission_verification == ControlStateVerification::changed
                      ? "invalid-command-state"
                      : "backend-unavailable",
                  admission_verification == ControlStateVerification::changed
                      ? "production state changed before critical command admission"
                      : "production state could not be verified before critical command admission");
  }

  CriticalAction accepted;
  json accepted_response;
  {
    std::unique_lock lock(mutex_);
    if (const auto found = known_by_action_id_.find(*action_id); found != known_by_action_id_.end())
    {
      if (found->second.payload_signature != signature)
      {
        return block_admission(lock,
                               device,
                               *action_id,
                               "protocol-violation",
                               "Duplicate critical action ID was reused with a different payload.");
      }
      return {found->second.action.status == CriticalActionStatus::completed ? 200 : 202,
              action_json(found->second.action)};
    }
    if (configuration_reserved_)
      return reject(*action_id, 409, "collection-load-in-progress");
    if (blocked_)
      return reject(*action_id, 409, blocked_reason_, blocked_message_);
    if (queue_.size() >= queue_capacity)
    {
      return block_admission(lock,
                             device,
                             *action_id,
                             "critical-queue-full",
                             "The critical live control FIFO is full.");
    }
    const auto sequence_key = device.id;
    if (const auto found = next_client_sequence_.find(sequence_key);
        found != next_client_sequence_.end() && *client_sequence != found->second)
    {
      sequence_recovery_ = SequenceRecovery{device.id, *action_id, *client_sequence, found->second};
      return block_admission(lock,
                             device,
                             *action_id,
                             "client-sequence-gap",
                             "Critical action client sequence is not contiguous.");
    }

    accepted.action_id = *action_id;
    accepted.device = device;
    accepted.client_sequence = *client_sequence;
    accepted.fifo_sequence = next_fifo_sequence_++;
    accepted.type = *action_type;
    accepted.transition_id = std::move(transition_id);
    accepted.destination_id = std::move(destination_id);
    accepted.accepted_monotonic_ns = monotonic_ns();
    queue_.push_back(accepted);
    known_by_action_id_.insert_or_assign(*action_id, KnownAction{accepted, signature});
    next_client_sequence_[sequence_key] = *client_sequence + 1;
    accepted_response = action_json(accepted);
  }

  audit_.command(device.id,
                 critical_action_wire_name(accepted.type),
                 json{{"clientSequence", accepted.client_sequence}}.dump(),
                 "critical_accepted",
                 accepted.action_id);
  emit_action("criticalActionAccepted", accepted);
  changed_.notify_all();
  return {202, std::move(accepted_response)};
}

CriticalSubmitResult CriticalLiveControlService::block_admission(std::unique_lock<std::mutex>& lock,
                                                                 const Device& device,
                                                                 std::string_view action_id,
                                                                 std::string reason,
                                                                 std::string message)
{
  blocked_ = true;
  blocked_reason_ = reason;
  blocked_message_ = message;
  blocked_at_monotonic_ns_ = monotonic_ns();
  // Queued live intents cannot survive recovery. The worker retains any executing action.
  cancel_queued_locked(reason, message);
  json evidence = {{"blockReason", reason}};
  if (sequence_recovery_)
  {
    const auto& recovery = *sequence_recovery_;
    evidence.update({{"originDeviceId", recovery.device_id},
                     {"originActionId", recovery.action_id},
                     {"submittedSequence", recovery.submitted_sequence},
                     {"expectedSequenceBefore", recovery.expected_sequence},
                     {"expectedSequenceAfter", recovery.expected_sequence}});
  }
  lock.unlock();
  audit_.command(device.id, "critical-admission", evidence.dump(), "critical_rejected", action_id);
  emit_controls_blocked(reason, message);
  return reject(action_id, 409, std::move(reason), std::move(message));
}

CriticalSubmitResult CriticalLiveControlService::acknowledge_block(const Device& device,
                                                                   const json& body)
{
  if (device.role < Role::operator_)
    return reject("", 403, "unauthorized", "operator role required");

  const auto request_id = required_string(body, "requestId");
  if (!request_id || request_id->size() > 128)
    return reject("", 422, "invalid-command-state", "valid requestId required");

  json recovery_evidence;
  {
    std::lock_guard lock(mutex_);
    if (!blocked_)
    {
      return {200,
              {{"requestId", *request_id},
               {"status", "UNCHANGED"},
               {"blocked", false},
               {"reason", nullptr},
               {"message", nullptr}}};
    }

    if (!can_unblock_locked())
    {
      return {409,
              {{"requestId", *request_id},
               {"status", "REJECTED"},
               {"blocked", true},
               {"reason", blocked_reason_},
               {"message", blocked_message_}}};
    }

    recovery_evidence = {{"blockReason", blocked_reason_}, {"acknowledgingDeviceId", device.id}};
    if (sequence_recovery_)
    {
      const auto& recovery = *sequence_recovery_;
      recovery_evidence.update({{"originDeviceId", recovery.device_id},
                                {"originActionId", recovery.action_id},
                                {"submittedSequence", recovery.submitted_sequence},
                                {"expectedSequenceBefore", recovery.expected_sequence},
                                {"expectedSequenceAfter", nullptr}});
      next_client_sequence_.erase(recovery.device_id);
      sequence_recovery_.reset();
    }
    blocked_ = false;
    blocked_reason_.clear();
    blocked_message_.clear();
    blocked_at_monotonic_ns_ = 0;
  }
  audit_.command(device.id,
                 "critical-recovery",
                 recovery_evidence.dump(),
                 "critical_acknowledged",
                 *request_id);
  changed_.notify_all();
  emit_controls_unblocked(*request_id);
  return {200,
          {{"requestId", *request_id},
           {"status", "ACKNOWLEDGED"},
           {"blocked", false},
           {"reason", nullptr},
           {"message", nullptr}}};
}

json CriticalLiveControlService::status(std::string_view action_id, const Device& requester) const
{
  std::lock_guard lock(mutex_);
  const auto found = known_by_action_id_.find(std::string(action_id));
  if (found == known_by_action_id_.end())
    return {{"status", "UNKNOWN_OR_EXPIRED"}, {"reason", "live-action-window-expired"}};
  if (requester.role < Role::admin && found->second.action.device.id != requester.id)
    return {{"status", "REJECTED"}, {"reason", "unauthorized"}};
  return action_json(found->second.action);
}

json CriticalLiveControlService::queue_health() const
{
  std::lock_guard lock(mutex_);
  const auto now = monotonic_ns();
  json head = nullptr;
  std::uint64_t head_age_ms = 0;
  if (current_)
  {
    head = current_->action_id;
    head_age_ms = (now - current_->accepted_monotonic_ns) / 1'000'000ULL;
  }
  else if (!queue_.empty())
  {
    head = queue_.front().action_id;
    head_age_ms = (now - queue_.front().accepted_monotonic_ns) / 1'000'000ULL;
  }
  return {{"available", started_.load()},
          {"status",
           blocked_          ? "faulted"
           : started_.load() ? "running"
                             : "idle"},
          {"blocked", blocked_},
          {"queueDepth", queue_.size() + (current_ ? 1U : 0U)},
          {"headActionId", head},
          {"headAgeMs", head_age_ms},
          {"lastFaultReason", blocked_reason_.empty() ? json(nullptr) : json(blocked_reason_)},
          {"lastFaultMessage", blocked_message_.empty() ? json(nullptr) : json(blocked_message_)},
          {"lastFaultAtMonotonicNs",
           blocked_at_monotonic_ns_ == 0 ? json(nullptr) : json(blocked_at_monotonic_ns_)},
          {"terminalHistorySize", history_.size()}};
}

void CriticalLiveControlService::observe_obs_connection(bool connected)
{
  if (connected)
    return;
  std::unique_lock lock(mutex_);
  if (queue_.empty() && !current_)
    return;
  blocked_ = true;
  blocked_reason_ = "obs-disconnected";
  blocked_message_ = "OBS disconnected while critical live controls were queued or executing.";
  blocked_at_monotonic_ns_ = monotonic_ns();
  cancel_queued_locked(blocked_reason_, blocked_message_);
  const auto reason = blocked_reason_;
  const auto message = blocked_message_;
  lock.unlock();
  emit_controls_blocked(reason, message);
}

std::string CriticalLiveControlService::payload_signature(const Device& device,
                                                          std::uint64_t client_sequence,
                                                          CriticalActionType type,
                                                          std::string_view transition_id,
                                                          std::string_view destination_id) const
{
  return device.id + ":" + std::to_string(client_sequence) + ":" +
         std::string(critical_action_wire_name(type)) + ":" + std::string(transition_id) + ":" +
         std::string(destination_id);
}

CriticalSubmitResult CriticalLiveControlService::reject(std::string_view action_id,
                                                        int http_status,
                                                        std::string reason,
                                                        std::string message)
{
  return {http_status,
          {{"actionId", action_id},
           {"status", "REJECTED"},
           {"reason", std::move(reason)},
           {"message", std::move(message)}}};
}

json CriticalLiveControlService::action_json(const CriticalAction& action) const
{
  return {
      {"actionId", action.action_id},
      {"deviceId", action.device.id},
      {"clientSequence", action.client_sequence},
      {"fifoSequence", action.fifo_sequence},
      {"action", critical_action_wire_name(action.type)},
      {"transitionId", action.transition_id.empty() ? json(nullptr) : json(action.transition_id)},
      {"destinationId",
       action.destination_id.empty() ? json(nullptr) : json(action.destination_id)},
      {"status", public_status(action.status)},
      {"acceptedAtMonotonicNs", action.accepted_monotonic_ns},
      {"queueDepth", queue_.size() + (current_ ? 1U : 0U)},
      {"reason", action.reason.empty() ? json(nullptr) : json(action.reason)},
      {"message", action.message.empty() ? json(nullptr) : json(action.message)},
      {"evidence", action.evidence}};
}

void CriticalLiveControlService::worker_loop()
{
  while (true)
  {
    CriticalAction action;
    {
      std::unique_lock lock(mutex_);
      changed_.wait(lock, [this] { return stopping_ || (!blocked_ && !queue_.empty()); });
      if (stopping_)
        return;
      if (blocked_ || queue_.empty())
        continue;
      action = queue_.front();
      queue_.pop_front();
      action.status = CriticalActionStatus::executing;
      current_ = action;
      known_by_action_id_.insert_or_assign(action.action_id,
                                           KnownAction{action,
                                                       payload_signature(action.device,
                                                                         action.client_sequence,
                                                                         action.type,
                                                                         action.transition_id,
                                                                         action.destination_id)});
    }
    emit_action("criticalActionExecuting", action);
    execute(std::move(action));
    {
      std::lock_guard lock(mutex_);
      current_.reset();
    }
    changed_.notify_all();
  }
}

void CriticalLiveControlService::execute(CriticalAction action)
{
  if (!obs_.connected())
  {
    fault(std::move(action), "obs-disconnected", "OBS is not connected.");
    return;
  }

  const auto snapshot = state_.command_state_snapshot();
  if (!snapshot.fresh())
  {
    fault(std::move(action), "obs-state-stale", "OBS state is not synchronized.");
    return;
  }

  const auto execution_verification = verify_control_state(action.type, snapshot);
  if (execution_verification != ControlStateVerification::matched)
  {
    if (request_reconcile_)
      request_reconcile_();
    fault(std::move(action),
          execution_verification == ControlStateVerification::changed ? "invalid-command-state"
                                                                      : "backend-unavailable",
          execution_verification == ControlStateVerification::changed
              ? "Production state changed before critical command execution."
              : "Production state could not be verified before critical command execution.");
    return;
  }

  if (action.type == CriticalActionType::transition_trigger)
  {
    if (!snapshot.studio_mode())
    {
      fault(std::move(action), "studio-mode-disabled", "Studio Mode is disabled.");
      return;
    }
    if (!action.transition_id.empty() && !snapshot.transition(action.transition_id))
    {
      fault(std::move(action), "resource-stale", "The selected transition is no longer available.");
      return;
    }
  }
  else if (output_satisfied(action.type, snapshot, action.destination_id))
  {
    complete(std::move(action), "COMPLETED_SATISFIED", {{"alreadySatisfied", true}});
    return;
  }
  else if ((action.type == CriticalActionType::stream_start ||
            action.type == CriticalActionType::stream_destination_start) &&
           !snapshot.stream_configured() &&
           obs_.backend_info().capabilities.stream_configuration_known)
  {
    fault(
        std::move(action), "output-not-configured", "Stream destination is not configured in OBS.");
    return;
  }
  else if ((action.type == CriticalActionType::record_pause ||
            action.type == CriticalActionType::record_resume) &&
           !obs_.recording_pause_available())
  {
    fault(std::move(action),
          "capability-unavailable",
          "OBS has disabled recording pause and resume for the current output.");
    return;
  }
  if (action.type == CriticalActionType::stream_destination_start ||
      action.type == CriticalActionType::stream_destination_stop)
  {
    const auto destination = snapshot.streaming_destination(action.destination_id);
    if (!destination)
    {
      fault(std::move(action), "resource-stale", "The selected stream destination is unavailable.");
      return;
    }
    if (destination->control_scope != "individual")
    {
      fault(std::move(action),
            "capability-unavailable",
            "The selected stream destination cannot be controlled individually.");
      return;
    }
    if (action.type == CriticalActionType::stream_destination_start &&
        (!destination->configured || !destination->enabled))
    {
      fault(std::move(action),
            "output-not-configured",
            "The selected stream destination is not configured.");
      return;
    }
  }

  auto transition_duration_ms = snapshot.transition_duration_ms();
  if (action.type == CriticalActionType::transition_trigger && !action.transition_id.empty())
    transition_duration_ms = snapshot.transition(action.transition_id)->duration_ms;

  const auto request = action.type == CriticalActionType::transition_trigger
                           ? std::string("TriggerStudioModeTransition")
                           : obs_request_for(action.type);
  json request_data = json::object();
  if (action.type == CriticalActionType::transition_trigger && !action.transition_id.empty())
  {
    const auto transition = snapshot.transition(action.transition_id);
    request_data = {{"transitionName", transition->name},
                    {"transitionUuid", transition->backend_id},
                    {"quickTransitionId", transition->id},
                    {"transitionDuration", transition->duration_ms}};
  }
  else if (action.type == CriticalActionType::stream_destination_start ||
           action.type == CriticalActionType::stream_destination_stop)
  {
    request_data = {{"destinationId", action.destination_id}};
  }
  try
  {
    auto request_done = std::async(
        std::launch::async,
        [&]
        {
          return request_executor_
                     ? request_executor_->request(ObsRequestLane::critical, request, request_data)
                     : obs_.request(request, request_data);
        });
    if (request_done.wait_for(obs_response_timeout) != std::future_status::ready)
    {
      fault(std::move(action), "obs-request-timeout", "OBS did not respond in time.");
      return;
    }
    static_cast<void>(request_done.get());
  }
  catch (const std::exception& error)
  {
    fault(std::move(action), "obs-temporary-rejection", std::string(error.what()).substr(0, 240));
    return;
  }

  action.status = CriticalActionStatus::verifying;
  {
    std::lock_guard lock(mutex_);
    current_ = action;
    known_by_action_id_.insert_or_assign(action.action_id,
                                         KnownAction{action,
                                                     payload_signature(action.device,
                                                                       action.client_sequence,
                                                                       action.type,
                                                                       action.transition_id,
                                                                       action.destination_id)});
  }
  emit_action("criticalActionExecuting", action, {{"phase", "VERIFYING"}});

  const bool converged = action.type == CriticalActionType::transition_trigger
                             ? wait_for_transition(snapshot, transition_duration_ms)
                             : wait_for_output(action.type, action.destination_id);
  if (!converged)
  {
    fault(std::move(action),
          action.type == CriticalActionType::transition_trigger ? "transition-result-ambiguous"
                                                                : "obs-convergence-timeout",
          "OBS did not confirm the critical live control in time.");
    return;
  }

  complete(std::move(action), "COMPLETED_APPLIED", {{"alreadySatisfied", false}});
}

void CriticalLiveControlService::complete(CriticalAction action, std::string outcome, json evidence)
{
  action.status = CriticalActionStatus::completed;
  action.evidence = std::move(evidence);
  action.evidence["outcome"] = outcome;
  {
    std::lock_guard lock(mutex_);
    known_by_action_id_.insert_or_assign(action.action_id,
                                         KnownAction{action,
                                                     payload_signature(action.device,
                                                                       action.client_sequence,
                                                                       action.type,
                                                                       action.transition_id,
                                                                       action.destination_id)});
    history_.push_back(KnownAction{action,
                                   payload_signature(action.device,
                                                     action.client_sequence,
                                                     action.type,
                                                     action.transition_id,
                                                     action.destination_id)});
    while (history_.size() > terminal_history_capacity)
    {
      known_by_action_id_.erase(history_.front().action.action_id);
      history_.pop_front();
    }
  }
  audit_.command(action.device.id,
                 critical_action_wire_name(action.type),
                 "",
                 "critical_completed",
                 action.action_id);
  emit_action("criticalActionCompleted", action, {{"outcome", std::move(outcome)}});
  changed_.notify_all();
}

void CriticalLiveControlService::fault(CriticalAction action,
                                       std::string reason,
                                       std::string message)
{
  action.status = CriticalActionStatus::faulted;
  action.reason = reason;
  action.message = message;
  {
    std::lock_guard lock(mutex_);
    blocked_ = true;
    blocked_reason_ = reason;
    blocked_message_ = message;
    blocked_at_monotonic_ns_ = monotonic_ns();
    known_by_action_id_.insert_or_assign(action.action_id,
                                         KnownAction{action,
                                                     payload_signature(action.device,
                                                                       action.client_sequence,
                                                                       action.type,
                                                                       action.transition_id,
                                                                       action.destination_id)});
    cancel_queued_locked(reason, message);
  }
  audit_.command(action.device.id,
                 critical_action_wire_name(action.type),
                 json{{"clientSequence", action.client_sequence}, {"reason", reason}}.dump(),
                 "critical_faulted",
                 action.action_id);
  emit_action("criticalActionFaulted", action, {{"reason", reason}, {"message", message}});
  emit_controls_blocked(std::move(reason), std::move(message));
  changed_.notify_all();
}

void CriticalLiveControlService::cancel_queued_locked(std::string reason, std::string message)
{
  for (auto& action : queue_)
  {
    action.status = CriticalActionStatus::canceled;
    action.reason = reason;
    action.message = message;
    known_by_action_id_.insert_or_assign(action.action_id,
                                         KnownAction{action,
                                                     payload_signature(action.device,
                                                                       action.client_sequence,
                                                                       action.type,
                                                                       action.transition_id,
                                                                       action.destination_id)});
  }
  queue_.clear();
}

bool CriticalLiveControlService::can_unblock_locked() const
{
  return !current_ && queue_.empty() && state_.fresh() && obs_.connected();
}

void CriticalLiveControlService::emit_action(std::string_view type,
                                             const CriticalAction& action,
                                             json extra)
{
  json message = {
      {"type", type},
      {"_deviceId", action.device.id},
      {"actionId", action.action_id},
      {"clientSequence", action.client_sequence},
      {"fifoSequence", action.fifo_sequence},
      {"action", critical_action_wire_name(action.type)},
      {"transitionId", action.transition_id.empty() ? json(nullptr) : json(action.transition_id)},
      {"destinationId",
       action.destination_id.empty() ? json(nullptr) : json(action.destination_id)},
      {"status", public_status(action.status)},
      {"queueDepth", queue_health()["queueDepth"]},
      {"createdAtMonotonicNs", action.accepted_monotonic_ns},
      {"updatedAtMonotonicNs", monotonic_ns()}};
  message.update(std::move(extra));
  MessageSink sink;
  {
    std::lock_guard lock(message_sink_mutex_);
    sink = message_sink_;
  }
  if (sink)
    sink(message);
}

void CriticalLiveControlService::emit_controls_blocked(std::string reason, std::string message)
{
  json value = {{"type", "criticalControlsBlocked"},
                {"reason", std::move(reason)},
                {"message", std::move(message)},
                {"blockedAtMonotonicNs", monotonic_ns()}};
  MessageSink sink;
  {
    std::lock_guard lock(message_sink_mutex_);
    sink = message_sink_;
  }
  if (sink)
    sink(value);
}

void CriticalLiveControlService::emit_controls_unblocked(std::string_view request_id)
{
  json value = {{"type", "criticalControlsUnblocked"},
                {"requestId", request_id},
                {"unblockedAtMonotonicNs", monotonic_ns()}};
  MessageSink sink;
  {
    std::lock_guard lock(message_sink_mutex_);
    sink = message_sink_;
  }
  if (sink)
    sink(value);
}

bool CriticalLiveControlService::output_satisfied(CriticalActionType type,
                                                  const CommandStateSnapshot& snapshot,
                                                  std::string_view destination_id) const
{
  const auto destination_active = [](const StreamingDestination& destination)
  { return destination.state == "streaming" || destination.state == "reconnecting"; };
  switch (type)
  {
  case CriticalActionType::record_start:
    return snapshot.record_active() && !snapshot.record_paused();
  case CriticalActionType::record_pause:
    return snapshot.record_active() && snapshot.record_paused();
  case CriticalActionType::record_resume:
    return snapshot.record_active() && !snapshot.record_paused();
  case CriticalActionType::record_stop:
    return !snapshot.record_active();
  case CriticalActionType::stream_start:
    return snapshot.stream_active();
  case CriticalActionType::stream_stop:
    return !snapshot.stream_active();
  case CriticalActionType::stream_destination_start:
    if (const auto destination = snapshot.streaming_destination(destination_id))
      return destination_active(*destination);
    return false;
  case CriticalActionType::stream_destination_stop:
    if (const auto destination = snapshot.streaming_destination(destination_id))
      return !destination_active(*destination);
    return false;
  case CriticalActionType::transition_trigger:
    return false;
  }
  return false;
}

std::vector<ControlStateExpectation>
CriticalLiveControlService::control_expectations(CriticalActionType type,
                                                 const CommandStateSnapshot& snapshot) const
{
  const auto capabilities = obs_.backend_info().capabilities;
  if ((type == CriticalActionType::stream_start || type == CriticalActionType::stream_stop) &&
      !capabilities.explicit_start_stop_for_stream)
  {
    return {{"output:stream",
             "GetStreamStatus",
             json::object(),
             "outputActive",
             snapshot.stream_active()}};
  }
  if ((type == CriticalActionType::record_start || type == CriticalActionType::record_stop) &&
      !capabilities.explicit_start_stop_for_record)
  {
    return {{"output:record:active",
             "GetRecordStatus",
             json::object(),
             "outputActive",
             snapshot.record_active()}};
  }
  if (type == CriticalActionType::record_pause || type == CriticalActionType::record_resume)
  {
    return {{"output:record:active",
             "GetRecordStatus",
             json::object(),
             "outputActive",
             snapshot.record_active()},
            {"output:record:paused",
             "GetRecordStatus",
             json::object(),
             "outputPaused",
             snapshot.record_paused()}};
  }
  if (type != CriticalActionType::transition_trigger)
    return {};

  std::vector<ControlStateExpectation> expectations = {{"studio-mode",
                                                        "GetStudioModeEnabled",
                                                        json::object(),
                                                        "studioModeEnabled",
                                                        snapshot.studio_mode()}};
  if (const auto program = snapshot.scene(snapshot.program_scene_id()))
  {
    const bool use_backend_id =
        program->backend_identifier_preferred && !program->backend_id.empty();
    expectations.push_back({"composition:program",
                            "GetCurrentProgramScene",
                            json::object(),
                            use_backend_id ? "currentProgramSceneUuid" : "currentProgramSceneName",
                            use_backend_id ? program->backend_id : program->name});
  }
  if (const auto preview = snapshot.scene(snapshot.preview_scene_id()))
  {
    const bool use_backend_id =
        preview->backend_identifier_preferred && !preview->backend_id.empty();
    expectations.push_back({"composition:preview",
                            "GetCurrentPreviewScene",
                            json::object(),
                            use_backend_id ? "currentPreviewSceneUuid" : "currentPreviewSceneName",
                            use_backend_id ? preview->backend_id : preview->name});
  }
  return expectations;
}

ControlStateVerification
CriticalLiveControlService::verify_control_state(CriticalActionType type,
                                                 const CommandStateSnapshot& snapshot)
{
  if (!obs_.backend_info().capabilities.authoritative_control_verification)
    return ControlStateVerification::matched;
  if (!snapshot.fresh())
    return ControlStateVerification::unavailable;

  for (const auto& expectation : control_expectations(type, snapshot))
  {
    try
    {
      const auto response =
          request_executor_
              ? request_executor_->request_current(ObsRequestLane::critical,
                                                   expectation.request_type,
                                                   expectation.request_data,
                                                   std::source_location::current(),
                                                   "authoritative critical control-state guard")
              : obs_.request_current(expectation.request_type,
                                     expectation.request_data,
                                     std::source_location::current(),
                                     "authoritative critical control-state guard");
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

std::string CriticalLiveControlService::obs_request_for(CriticalActionType type) const
{
  switch (type)
  {
  case CriticalActionType::record_start:
    return "StartRecord";
  case CriticalActionType::record_pause:
    return "PauseRecord";
  case CriticalActionType::record_resume:
    return "ResumeRecord";
  case CriticalActionType::record_stop:
    return "StopRecord";
  case CriticalActionType::stream_start:
    return "StartStream";
  case CriticalActionType::stream_stop:
    return "StopStream";
  case CriticalActionType::stream_destination_start:
    return "StartStreamDestination";
  case CriticalActionType::stream_destination_stop:
    return "StopStreamDestination";
  case CriticalActionType::transition_trigger:
    break;
  }
  return {};
}

bool CriticalLiveControlService::wait_for_output(CriticalActionType type,
                                                 std::string_view destination_id)
{
  const auto deadline = Clock::now() + output_convergence_timeout;
  while (!stopping_ && Clock::now() < deadline)
  {
    if (output_satisfied(type, state_.command_state_snapshot(), destination_id))
      return true;
    std::this_thread::sleep_for(poll_interval);
  }
  return false;
}

bool CriticalLiveControlService::wait_for_transition(const CommandStateSnapshot& baseline,
                                                     std::uint32_t transition_duration_ms)
{
  const auto timeout =
      std::chrono::milliseconds(transition_duration_ms) + std::chrono::milliseconds(1000);
  const auto deadline = Clock::now() + timeout;
  while (!stopping_ && Clock::now() < deadline)
  {
    const auto current = state_.command_state_snapshot();
    if (!current.fresh())
      return false;
    if ((!baseline.preview_scene_id().empty() &&
         current.program_scene_id() == baseline.preview_scene_id()) ||
        current.program_frame_ns() > baseline.program_frame_ns())
      return true;
    std::this_thread::sleep_for(poll_interval);
  }
  return false;
}

CriticalLiveControlService::ConfigurationReservation::~ConfigurationReservation()
{
  std::lock_guard lock(owner_.mutex_);
  owner_.configuration_reserved_ = false;
}

std::unique_ptr<CriticalLiveControlService::ConfigurationReservation>
CriticalLiveControlService::reserve_configuration()
{
  std::lock_guard lock(mutex_);
  if (!started_ || stopping_ || configuration_reserved_ || current_ || !queue_.empty() || blocked_)
    return {};
  auto reservation = std::unique_ptr<ConfigurationReservation>(new ConfigurationReservation(*this));
  configuration_reserved_ = true;
  return reservation;
}

} // namespace studio_remote
