#pragma once

#include <studio_remote/ui/management_controller.hpp>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace studio_remote::macos
{
struct ManagementUpdate
{
  ManagementUiState state;
  bool command_result = false;
  TeleprompterUiState teleprompter;
};

/// Serial execution context for the shared management controller. Backend callbacks run only on
/// this worker, with an autorelease pool per operation. The root invalidates the native bridge,
/// stops/joins this worker, then destroys backend callback targets. One external lifecycle owner
/// stops it; snapshot callbacks run on the worker and must not stop/destroy it.
class ManagementWorker final
{
public:
  using Command = std::function<void(ManagementUiController&)>;
  using SnapshotHandler = std::function<void(ManagementUpdate)>;

  ManagementWorker(ManagementInterfaceMode mode,
                   ManagementPage initial_page,
                   std::function<void(ManagementInterfaceMode)> mode_changed,
                   ManagementUiCallbacks callbacks);
  ~ManagementWorker();
  ManagementWorker(const ManagementWorker&) = delete;
  ManagementWorker& operator=(const ManagementWorker&) = delete;

  /// Nonblocking enqueue. A full queue or stopped worker rejects new work with an error.
  void post(Command command);
  /// Consecutive queued refreshes coalesce; no timer or hidden-window polling is owned here.
  void refresh();
  ManagementUiState snapshot() const;
  void on_snapshot(SnapshotHandler handler);
  /// Drops pending work, lets the current bounded backend operation finish, and joins.
  void stop();

private:
  struct Job
  {
    Command command;
    bool refresh = false;
  };
  void enqueue(Job job);
  void run(std::stop_token stop);
  void apply(Job job);
  void publish(bool command_result);

  ManagementUiController controller_;
  mutable std::mutex snapshot_mutex_;
  ManagementUiState snapshot_;
  SnapshotHandler handler_;
  std::mutex queue_mutex_;
  std::condition_variable_any wake_;
  std::deque<Job> jobs_;
  bool accepting_ = true;
  std::jthread thread_;
};
} // namespace studio_remote::macos
