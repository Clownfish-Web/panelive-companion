#pragma once

#include <studio_remote/critical/critical_live_control.hpp>
#include <studio_remote/obs/obs_adapter.hpp>
#include <studio_remote/obs/obs_request_arbiter.hpp>
#include <studio_remote/provider/control_state_verification.hpp>
#include <studio_remote/state/state_store.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace studio_remote
{

class CriticalLiveControlService
{
public:
  using MessageSink = std::function<void(const json&)>;
  using ReconcileRequest = std::function<void()>;

  class ConfigurationReservation
  {
  public:
    ~ConfigurationReservation();
    ConfigurationReservation(const ConfigurationReservation&) = delete;
    ConfigurationReservation& operator=(const ConfigurationReservation&) = delete;

  private:
    friend class CriticalLiveControlService;
    explicit ConfigurationReservation(CriticalLiveControlService& owner) : owner_(owner)
    {
    }
    CriticalLiveControlService& owner_;
  };

  // Excludes live-output admission until the queued configuration operation finishes.
  std::unique_ptr<ConfigurationReservation> reserve_configuration();

  CriticalLiveControlService(IObsAdapter& obs,
                             StateStore& state,
                             AuditStore& audit,
                             IObsRequestExecutor* request_executor = nullptr,
                             ReconcileRequest request_reconcile = {});
  ~CriticalLiveControlService();

  CriticalLiveControlService(const CriticalLiveControlService&) = delete;
  CriticalLiveControlService& operator=(const CriticalLiveControlService&) = delete;

  void start();
  void stop();
  bool available() const noexcept;
  void set_message_sink(MessageSink sink);

  CriticalSubmitResult submit(const Device& device, const json& body);
  CriticalSubmitResult acknowledge_block(const Device& device, const json& body);
  json status(std::string_view action_id, const Device& requester) const;
  json queue_health() const;
  void observe_obs_connection(bool connected);

private:
  struct KnownAction
  {
    CriticalAction action;
    std::string payload_signature;
  };

  struct SequenceRecovery
  {
    std::string device_id;
    std::string action_id;
    std::uint64_t submitted_sequence;
    std::uint64_t expected_sequence;
  };

  std::string payload_signature(const Device& device,
                                std::uint64_t client_sequence,
                                CriticalActionType type,
                                std::string_view transition_id,
                                std::string_view destination_id) const;
  CriticalSubmitResult
  reject(std::string_view action_id, int http_status, std::string reason, std::string message = {});
  CriticalSubmitResult block_admission(std::unique_lock<std::mutex>& lock,
                                       const Device& device,
                                       std::string_view action_id,
                                       std::string reason,
                                       std::string message);
  json action_json(const CriticalAction& action) const;
  void worker_loop();
  void execute(CriticalAction action);
  void complete(CriticalAction action, std::string outcome, json evidence);
  void fault(CriticalAction action, std::string reason, std::string message);
  void cancel_queued_locked(std::string reason, std::string message);
  bool can_unblock_locked() const;
  void
  emit_action(std::string_view type, const CriticalAction& action, json extra = json::object());
  void emit_controls_blocked(std::string reason, std::string message);
  void emit_controls_unblocked(std::string_view request_id);
  bool output_satisfied(CriticalActionType type,
                        const CommandStateSnapshot& snapshot,
                        std::string_view destination_id = {}) const;
  std::vector<ControlStateExpectation>
  control_expectations(CriticalActionType type, const CommandStateSnapshot& snapshot) const;
  ControlStateVerification verify_control_state(CriticalActionType type,
                                                const CommandStateSnapshot& snapshot);
  std::string obs_request_for(CriticalActionType type) const;
  bool wait_for_output(CriticalActionType type, std::string_view destination_id = {});
  bool wait_for_transition(const CommandStateSnapshot& baseline,
                           std::uint32_t transition_duration_ms);

  IObsAdapter& obs_;
  IObsRequestExecutor* request_executor_ = nullptr;
  StateStore& state_;
  AuditStore& audit_;
  ReconcileRequest request_reconcile_;

  std::atomic<bool> stopping_{false};
  std::atomic<bool> started_{false};
  std::thread worker_;

  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<CriticalAction> queue_;
  std::deque<KnownAction> history_;
  std::unordered_map<std::string, KnownAction> known_by_action_id_;
  std::unordered_map<std::string, std::uint64_t> next_client_sequence_;
  // Guarded by mutex_; retained across later faults until successful global recovery.
  std::optional<SequenceRecovery> sequence_recovery_;
  std::uint64_t next_fifo_sequence_ = 1;
  bool blocked_ = false;
  bool configuration_reserved_ = false;
  std::string blocked_reason_;
  std::string blocked_message_;
  std::uint64_t blocked_at_monotonic_ns_ = 0;
  std::optional<CriticalAction> current_;

  std::mutex message_sink_mutex_;
  MessageSink message_sink_;
};

} // namespace studio_remote
