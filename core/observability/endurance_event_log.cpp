#include "debug_log_files.hpp"
#include "endurance_snapshot_schedule.hpp"

#include <studio_remote/common/bounded_json_size.hpp>
#include <studio_remote/observability/endurance_event_log.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace studio_remote
{
EnduranceEventLog::EnduranceEventLog(std::filesystem::path directory,
                                     std::string companion_version,
                                     std::chrono::milliseconds snapshot_interval)
    : files_(std::make_unique<DebugLogFiles>(std::move(directory))),
      companion_version_(std::move(companion_version)), snapshot_interval_(snapshot_interval)
{
  if (snapshot_interval_ < std::chrono::milliseconds(100))
    throw std::invalid_argument("Endurance snapshot interval must be at least 100 ms");
  worker_ = std::thread([this] { worker_loop(); });
}

EnduranceEventLog::~EnduranceEventLog()
{
  stop();
  set_snapshot_provider({});
}

void EnduranceEventLog::set_snapshot_provider(SnapshotProvider provider)
{
  snapshot_source_.replace(std::move(provider));
  cv_.notify_all();
}

void EnduranceEventLog::enable()
{
  std::lock_guard lock(mutex_);
  if (stopping_)
    throw std::logic_error("Debug recorder has stopped");
  if (enabled_.exchange(true))
    return;
  ++generation_;
  status_.last_error.clear();
  cv_.notify_one();
}

void EnduranceEventLog::disable()
{
  enabled_.store(false, std::memory_order_release);
  cv_.notify_one();
}

void EnduranceEventLog::clear()
{
  std::lock_guard lock(mutex_);
  enabled_.store(false, std::memory_order_release);
  clear_requested_ = true;
  cv_.notify_one();
}

void EnduranceEventLog::stop()
{
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    enabled_.store(false, std::memory_order_release);
    cv_.notify_one();
  }
  if (worker_.joinable())
    worker_.join();
}

bool EnduranceEventLog::enabled() const noexcept
{
  return enabled_.load(std::memory_order_acquire);
}

void EnduranceEventLog::record(std::string_view category,
                               std::string_view event,
                               json details) noexcept
{
  if (!enabled())
    return;
  try
  {
    const auto size = bounded_json_size(details, 4096);
    if (!size || category.size() > 64 || event.size() > 128)
    {
      ++lost_events_;
      return;
    }
    const auto bytes = *size + sizeof(PendingEvent) + category.size() + event.size();
    std::lock_guard lock(mutex_);
    if (!enabled() || stopping_)
      return;
    const auto now = std::chrono::steady_clock::now();
    if (now - rate_window_ >= std::chrono::seconds(1))
    {
      rate_window_ = now;
      rate_events_ = 0;
      rate_bytes_ = 0;
    }
    if (pending_events_ >= maximum_queued_events || bytes > maximum_queued_bytes - pending_bytes_ ||
        rate_events_ >= 1000 || bytes > 1024 * 1024 - rate_bytes_)
    {
      ++lost_events_;
      return;
    }
    events_.push_back({unix_time_ms(),
                       monotonic_time_ns(),
                       std::string(category),
                       std::string(event),
                       std::move(details),
                       bytes,
                       generation_});
    pending_bytes_ += bytes;
    ++pending_events_;
    ++rate_events_;
    rate_bytes_ += bytes;
    cv_.notify_one();
  }
  catch (...)
  {
    // Diagnostic producer boundary: never turn an optional observation into a control failure.
    ++lost_events_;
  }
}

EnduranceLogStatus EnduranceEventLog::status() const
{
  std::lock_guard lock(mutex_);
  auto status = status_;
  status.enabled = enabled();
  status.queued_events = pending_events_;
  status.queued_bytes = pending_bytes_;
  status.dropped_events = lost_events_.load(std::memory_order_relaxed);
  return status;
}

std::uint64_t EnduranceEventLog::unix_time_ms() noexcept
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
}

std::uint64_t EnduranceEventLog::monotonic_time_ns() noexcept
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

void EnduranceEventLog::publish_file_status()
{
  std::lock_guard lock(mutex_);
  status_.file = files_->current_path();
  status_.retained_bytes = files_->retained_bytes();
}

void EnduranceEventLog::set_error(std::string_view message) noexcept
{
  enabled_.store(false, std::memory_order_release);
  ++lost_events_;
  try
  {
    std::lock_guard lock(mutex_);
    status_.last_error = std::string(message.substr(0, 240));
  }
  catch (...)
  {
    // Loss is still observable through the allocation-free counter above.
  }
}

void EnduranceEventLog::worker_loop()
{
  try
  {
    run_worker();
  }
  catch (const std::exception& error)
  {
    set_error(error.what());
  }
  catch (...)
  {
    set_error("Debug recorder worker failed");
  }
  // A terminated worker must never accept another session or retain stranded producer work.
  std::lock_guard lock(mutex_);
  stopping_ = true;
  enabled_.store(false, std::memory_order_release);
  lost_events_ += pending_events_;
  events_.clear();
  pending_events_ = 0;
  pending_bytes_ = 0;
}

void EnduranceEventLog::write_record(json value)
{
  const auto now = unix_time_ms();
  const auto observed = value.value("observedAtUnixMs", now);
  // Bound backlog age as well as bytes. Segment filenames use the same conservative age margin.
  if (observed > now || now - observed >= 60'000)
  {
    ++lost_events_;
    return;
  }
  auto line = value.dump();
  if (line.size() > 65'536)
  {
    ++lost_events_;
    return;
  }
  files_->append(line, now);
  std::lock_guard lock(mutex_);
  ++status_.records_written;
  status_.bytes_written += line.size() + 1;
}

void EnduranceEventLog::run_worker()
{
  using Clock = std::chrono::steady_clock;
  auto maintenance = Clock::now();
  auto flush_at = Clock::now() + std::chrono::seconds(1);
  auto started = Clock::now();
  EnduranceSnapshotSchedule snapshots(started, snapshot_interval_);
  std::uint64_t observed_generation = 0;
  bool session_open = false;
  for (;;)
  {
    std::deque<PendingEvent> batch;
    bool stopping, clear;
    std::uint64_t generation;
    {
      std::unique_lock lock(mutex_);
      auto deadline = maintenance;
      if (session_open)
        deadline = std::min({deadline, flush_at, snapshots.deadline()});
      cv_.wait_until(lock,
                     deadline,
                     [&]
                     {
                       return stopping_ || clear_requested_ || generation_ != observed_generation ||
                              !events_.empty() || (session_open && !enabled());
                     });
      stopping = stopping_;
      clear = std::exchange(clear_requested_, false);
      generation = generation_;
      for (std::size_t count = 0; count < 256 && !events_.empty(); ++count)
      {
        batch.push_back(std::move(events_.front()));
        events_.pop_front();
      }
    }
    try
    {
      if (clear)
      {
        files_->clear();
        session_open = false;
        observed_generation = enabled() ? generation - 1 : generation;
        std::lock_guard lock(mutex_);
        lost_events_ += events_.size() + batch.size();
        events_.clear();
        batch.clear();
        pending_bytes_ = 0;
        pending_events_ = 0;
      }
      if (generation != observed_generation)
      {
        files_->close();
        observed_generation = generation;
        started = Clock::now();
        snapshots.restart(started);
        write_record({{"recordType", "sessionStarted"},
                      {"observedAtUnixMs", unix_time_ms()},
                      {"observedAtMonotonicNs", monotonic_time_ns()},
                      {"schemaVersion", 1},
                      {"companionVersion", companion_version_}});
        session_open = true;
      }
      for (auto& event : batch)
      {
        if (session_open && event.generation == observed_generation)
          write_record({{"recordType", "event"},
                        {"observedAtUnixMs", event.unix_ms},
                        {"observedAtMonotonicNs", event.monotonic_ns},
                        {"category", std::move(event.category)},
                        {"event", std::move(event.event)},
                        {"details", std::move(event.details)}});
        else
          ++lost_events_;
      }
      const auto now = Clock::now();
      if (now - started >= std::chrono::hours(24))
        disable();
      if (session_open && enabled() && snapshots.poll(now))
      {
        if (auto snapshot = snapshot_source_.sample())
        {
          if (!bounded_json_size(*snapshot, 256 * 1024))
          {
            ++lost_events_;
            *snapshot = {{"truncated", true}, {"reason", "snapshot-budget-exceeded"}};
          }
          write_record({{"recordType", "snapshot"},
                        {"observedAtUnixMs", unix_time_ms()},
                        {"observedAtMonotonicNs", monotonic_time_ns()},
                        {"snapshot", std::move(*snapshot)}});
        }
      }
      bool drained;
      {
        std::lock_guard lock(mutex_);
        drained = events_.empty();
      }
      if (session_open && !enabled() && drained)
      {
        write_record({{"recordType", "sessionStopped"},
                      {"observedAtUnixMs", unix_time_ms()},
                      {"observedAtMonotonicNs", monotonic_time_ns()}});
        files_->close();
        session_open = false;
      }
      if (batch.size() == 256 || now >= flush_at)
      {
        files_->flush();
        flush_at = now + std::chrono::seconds(1);
      }
      if (now >= maintenance)
      {
        files_->maintain(unix_time_ms());
        maintenance = now + std::chrono::minutes(1);
      }
      publish_file_status();
    }
    catch (const std::exception& error)
    {
      set_error(error.what());
      session_open = false;
      maintenance = Clock::now() + std::chrono::minutes(1);
      try
      {
        files_->close();
      }
      catch (const std::exception& close_error)
      {
        set_error(close_error.what());
      }
    }
    {
      std::lock_guard lock(mutex_);
      for (const auto& event : batch)
      {
        pending_bytes_ -= event.bytes;
        --pending_events_;
      }
      if (stopping && events_.empty())
        return;
    }
  }
}
} // namespace studio_remote
