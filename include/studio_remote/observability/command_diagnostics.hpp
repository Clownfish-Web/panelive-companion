#pragma once

#include <studio_remote/common/types.hpp>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace studio_remote
{
class EnduranceEventLog;

/// Best-effort incident evidence, never command authority or replay state. The optional recorder
/// is borrowed for this object's lifetime; composition destroys command producers first.
class CommandDiagnostics final
{
public:
  using UnixTimeProvider = std::function<std::uint64_t()>;
  CommandDiagnostics(UnixTimeProvider now, EnduranceEventLog* recorder);
  void record(std::string_view device,
              std::string_view action,
              std::string_view target,
              std::string_view result,
              std::string_view request) noexcept;
  json recent(std::size_t limit);
  std::uint64_t dropped() const noexcept;

private:
  struct Entry
  {
    std::uint64_t unix_ms;
    std::chrono::steady_clock::time_point observed;
    std::string device, action, target, result, request;
  };
  static json value(const Entry& entry);
  void expire(std::uint64_t now, std::chrono::steady_clock::time_point observed);

  UnixTimeProvider now_;
  EnduranceEventLog* recorder_;
  std::mutex mutex_;
  std::deque<Entry> entries_;
  std::atomic<std::uint64_t> dropped_{0};
};
} // namespace studio_remote
