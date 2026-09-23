#include "management_worker.hpp"

#import <Foundation/Foundation.h>

#include <stdexcept>
#include <syslog.h>
#include <utility>

namespace studio_remote::macos
{
ManagementWorker::ManagementWorker(ManagementInterfaceMode mode,
                                   ManagementPage initial_page,
                                   std::function<void(ManagementInterfaceMode)> mode_changed,
                                   ManagementUiCallbacks callbacks)
    : controller_(mode, initial_page, std::move(mode_changed), std::move(callbacks)),
      snapshot_(controller_.state()), thread_([this](std::stop_token stop) { run(stop); })
{
}
ManagementWorker::~ManagementWorker()
{
  stop();
}

void ManagementWorker::post(Command command)
{
  if (!command)
    throw std::invalid_argument("A management command is required");
  enqueue({std::move(command), false});
}
void ManagementWorker::refresh()
{
  enqueue({[](ManagementUiController& controller) { controller.refresh(); }, true});
}
void ManagementWorker::enqueue(Job job)
{
  {
    std::lock_guard lock(queue_mutex_);
    if (!accepting_)
      throw std::runtime_error("Management is shutting down");
    if (job.refresh && !jobs_.empty() && jobs_.back().refresh)
      return;
    if (jobs_.size() >= 32)
      throw std::runtime_error("Management is busy; wait for pending operations to finish");
    jobs_.push_back(std::move(job));
  }
  wake_.notify_one();
}
ManagementUiState ManagementWorker::snapshot() const
{
  std::lock_guard lock(snapshot_mutex_);
  return snapshot_;
}
void ManagementWorker::on_snapshot(SnapshotHandler handler)
{
  std::lock_guard lock(snapshot_mutex_);
  handler_ = std::move(handler);
}

void ManagementWorker::stop()
{
  if (thread_.joinable() && thread_.get_id() == std::this_thread::get_id())
    throw std::logic_error("Management callbacks cannot stop their worker");
  {
    std::lock_guard lock(queue_mutex_);
    accepting_ = false;
    jobs_.clear();
  }
  thread_.request_stop();
  wake_.notify_all();
  if (thread_.joinable())
    thread_.join();
}

void ManagementWorker::apply(Job job)
{
  @autoreleasepool
  {
    try
    {
      job.command(controller_);
      controller_.expire_pairing();
    }
    catch (const std::exception& error)
    {
      controller_.report_error(error.what());
    }
    catch (...)
    {
      controller_.report_error("Management operation failed");
      syslog(LOG_ERR, "Unexpected management operation failure");
    }
    publish(!job.refresh);
  }
}
void ManagementWorker::publish(bool command_result)
{
  auto state = controller_.state();
  SnapshotHandler handler;
  {
    std::lock_guard lock(snapshot_mutex_);
    snapshot_ = state;
    handler = handler_;
  }
  if (handler)
    handler({std::move(state), command_result, controller_.teleprompter().state()});
}
void ManagementWorker::run(std::stop_token stop)
{
  try
  {
    while (!stop.stop_requested())
    {
      Job job;
      {
        std::unique_lock lock(queue_mutex_);
        if (!wake_.wait(lock, stop, [this] { return !jobs_.empty(); }))
          break;
        job = std::move(jobs_.front());
        jobs_.pop_front();
      }
      apply(std::move(job));
    }
  }
  catch (...)
  {
    syslog(LOG_ERR, "Management worker stopped after a delivery failure");
    std::lock_guard lock(queue_mutex_);
    accepting_ = false;
    jobs_.clear();
  }
}
} // namespace studio_remote::macos
