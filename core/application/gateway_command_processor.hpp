#pragma once

#include "critical_live_control_service.hpp"
#include <studio_remote/credentials/audit_store.hpp>
#include <studio_remote/obs/obs_adapter.hpp>
#include <studio_remote/provider/control_state_verification.hpp>
#include <studio_remote/state/command_state_snapshot.hpp>
#include <studio_remote/state/state_store.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>

namespace studio_remote
{

class GatewayCommandProcessor
{
public:
  using MessageSink = std::function<void(const json&)>;
  using ReconcileRequest = std::function<void()>;
  using ReceiptClock = std::function<Clock::time_point()>;

  GatewayCommandProcessor(IObsAdapter& obs,
                          StateStore& state,
                          AuditStore& audit,
                          ReconcileRequest request_reconcile,
                          ReceiptClock receipt_clock = Clock::now);
  ~GatewayCommandProcessor();

  GatewayCommandProcessor(const GatewayCommandProcessor&) = delete;
  GatewayCommandProcessor& operator=(const GatewayCommandProcessor&) = delete;

  json execute(const Device& device, std::string_view action, const json& body);
  json select_collection(const Device& device,
                         const ProductionCollectionSelection& selection,
                         IObsAdapter& provider,
                         CriticalLiveControlService& controls);

  void set_message_sink(MessageSink sink);
  void stop();

private:
  enum class CommandFailureCode
  {
    invalid_command_state,
    resource_stale,
    audio_track_read_only
  };

  struct CommandFailure
  {
    json response;
    CommandFailureCode code = CommandFailureCode::invalid_command_state;
  };

  struct CommandPlan
  {
    std::string obs_request;
    json obs_request_data = json::object();
    std::string audit_target;
    json completion_data = json::object();
    std::optional<std::string> resource_identity;
    std::vector<ControlStateExpectation> control_expectations;
    bool already_applied = false;
  };

  struct CacheInsertion
  {
    json response;
    bool inserted = false;
  };

  struct QueuedOperation
  {
    Device device;
    std::string action;
    CommandPlan plan;
    std::string operation_id;
  };

  struct QueuedCollectionSelection
  {
    Device device;
    ProductionCollectionSelection selection;
    std::uint64_t catalog_revision = 0;
    std::string operation_id;
    IObsAdapter* provider = nullptr; // Runtime owns the provider beyond the command worker.
    std::unique_ptr<CriticalLiveControlService::ConfigurationReservation> reservation;
  };

  using PreparedCommand = std::variant<CommandPlan, CommandFailure>;

  json command_result(const json& body, std::string status, std::string reason = {}) const;
  CommandFailure
  command_failure(const json& body,
                  std::string reason,
                  CommandFailureCode code = CommandFailureCode::invalid_command_state) const;

  PreparedCommand
  prepare_command(std::string_view action, const json& body, const CommandStateSnapshot& state);
  PreparedCommand prepare_scene_selection(const json& body, const CommandStateSnapshot& state);
  PreparedCommand prepare_scene_item_visibility(const json& body,
                                                const CommandStateSnapshot& state);
  static std::string_view failure_reason(CommandFailureCode code);
  PreparedCommand prepare_audio_mute(const json& body, const CommandStateSnapshot& state);
  PreparedCommand prepare_input_mute(const json& body, const CommandStateSnapshot& state);
  PreparedCommand prepare_input_solo(const json& body, const CommandStateSnapshot& state);
  PreparedCommand prepare_input_volume(const json& body, const CommandStateSnapshot& state);
  PreparedCommand prepare_input_balance(const json& body, const CommandStateSnapshot& state);
  PreparedCommand prepare_master_volume(const json& body, const CommandStateSnapshot& state);
  PreparedCommand prepare_input_monitoring(const json& body, const CommandStateSnapshot& state);
  PreparedCommand prepare_transition(const json& body, const CommandStateSnapshot& state);
  PreparedCommand prepare_output_command(std::string_view action,
                                         const json& body,
                                         const CommandStateSnapshot& state);

  bool
  queue_operation(Device device, std::string action, CommandPlan plan, std::string operation_id);
  void operation_loop();
  void run_collection_selection(const QueuedCollectionSelection& operation);
  void run_operation(const Device& device,
                     const std::string& action,
                     const CommandPlan& plan,
                     const std::string& operation_id);
  bool is_completed(std::string_view action, const json& completion_data) const;
  ControlStateVerification verify_control_state(const CommandPlan& plan);
  void emit_failure(std::string_view command_id,
                    std::string_view operation_id,
                    std::string_view reason_code,
                    std::string reason);

  std::optional<json> cached_response(const std::string& key);
  CacheInsertion cache_accepted_response(const std::string& key, json response);
  void remove_expired_cache_entries();
  void emit(json message);
  std::optional<std::uint32_t> reserve_transition(std::uint32_t transition_duration_ms);
  void release_transition();

  IObsAdapter& obs_;
  StateStore& state_;
  AuditStore& audit_;
  ReconcileRequest request_reconcile_;
  ReceiptClock receipt_clock_;

  std::atomic<bool> stopping_{false};

  std::mutex message_sink_mutex_;
  MessageSink message_sink_;

  std::mutex operation_mutex_;
  std::condition_variable operation_cv_;
  std::deque<std::variant<QueuedOperation, QueuedCollectionSelection>> operations_;
  std::thread operation_thread_;

  std::mutex transition_mutex_;
  std::size_t reserved_transitions_ = 0;
  Clock::time_point active_transition_deadline_{};

  std::mutex command_cache_mutex_;
  std::mutex collection_admission_mutex_;
  std::unordered_map<std::string, std::pair<Clock::time_point, json>> command_cache_;
  struct ReceiptExpiry
  {
    std::string key;
    std::size_t bytes;
  };
  std::deque<ReceiptExpiry> receipt_expiry_;
  std::size_t receipt_bytes_ = 0;
};

} // namespace studio_remote
