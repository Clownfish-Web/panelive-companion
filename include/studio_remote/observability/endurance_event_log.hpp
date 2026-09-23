#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/observability/endurance_snapshot_source.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace studio_remote
{
class DebugLogFiles;

struct EnduranceLogStatus
{
  bool enabled = false;
  std::filesystem::path file;
  std::uint64_t records_written = 0;
  std::uint64_t bytes_written = 0;
  std::uint64_t queued_events = 0;
  std::uint64_t dropped_events = 0;
  std::string last_error;
  std::uint64_t retained_bytes = 0;
  std::uint64_t queued_bytes = 0;
};

// Default-off bounded debug recorder. One owned worker also expires files while recording is off.
// Producers never perform filesystem work; lifecycle stop joins the worker before dependencies die.
class EnduranceEventLog
{
public:
  using SnapshotProvider = EnduranceSnapshotSource::Provider;

  EnduranceEventLog(std::filesystem::path directory,
                    std::string companion_version,
                    std::chrono::milliseconds snapshot_interval = std::chrono::seconds(5));
  ~EnduranceEventLog();

  EnduranceEventLog(const EnduranceEventLog&) = delete;
  EnduranceEventLog& operator=(const EnduranceEventLog&) = delete;

  /// Replacing/clearing joins an active snapshot. Call only from a lifecycle owner, never from
  /// the snapshot callback itself; its borrowed dependencies must outlive this call.
  void set_snapshot_provider(SnapshotProvider provider);
  void enable();
  void disable();
  void clear();
  void stop();
  bool enabled() const noexcept;
  void
  record(std::string_view category, std::string_view event, json details = json::object()) noexcept;
  EnduranceLogStatus status() const;

private:
  struct PendingEvent
  {
    std::uint64_t unix_ms = 0;
    std::uint64_t monotonic_ns = 0;
    std::string category;
    std::string event;
    json details;
    std::size_t bytes;
    std::uint64_t generation;
  };

  static std::uint64_t unix_time_ms() noexcept;
  static std::uint64_t monotonic_time_ns() noexcept;
  void worker_loop();
  void run_worker();
  void write_record(json value);
  void set_error(std::string_view message) noexcept;
  void publish_file_status();

  static constexpr std::size_t maximum_queued_events = 4096;
  static constexpr std::size_t maximum_queued_bytes = 4 * 1024 * 1024;

  std::unique_ptr<DebugLogFiles> files_;
  std::string companion_version_;
  std::chrono::milliseconds snapshot_interval_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  EnduranceSnapshotSource snapshot_source_;
  std::deque<PendingEvent> events_;
  std::thread worker_;
  EnduranceLogStatus status_;
  std::atomic<bool> enabled_{false};
  std::atomic<std::uint64_t> lost_events_{0};
  std::uint64_t generation_ = 0;
  std::size_t pending_bytes_ = 0;
  std::size_t pending_events_ = 0;
  std::chrono::steady_clock::time_point rate_window_{};
  std::size_t rate_events_ = 0;
  std::size_t rate_bytes_ = 0;
  bool clear_requested_ = false;
  bool stopping_ = false;
};

} // namespace studio_remote
