#include <studio_remote/observability/command_diagnostics.hpp>
#include <studio_remote/observability/endurance_event_log.hpp>

#include <algorithm>
#include <utility>

namespace studio_remote
{
namespace
{
constexpr std::size_t maximum_entries = 256;
constexpr std::size_t maximum_field_bytes = 128;
constexpr std::uint64_t retention_ms = 24ULL * 60 * 60 * 1000;

std::string bounded(std::string_view text, std::size_t maximum_bytes = maximum_field_bytes)
{
  auto size = std::min(text.size(), maximum_bytes);
  // Do not cut a UTF-8 code point. The protocol boundary validates incoming text.
  while (size < text.size() && size > 0 &&
         (static_cast<unsigned char>(text[size]) & 0xc0U) == 0x80U)
    --size;
  return std::string(text.substr(0, size));
}
} // namespace

CommandDiagnostics::CommandDiagnostics(UnixTimeProvider now, EnduranceEventLog* recorder)
    : now_(std::move(now)), recorder_(recorder)
{
}

json CommandDiagnostics::value(const Entry& entry)
{
  return {{"atUnixMs", entry.unix_ms},
          {"deviceId", entry.device},
          {"action", entry.action},
          {"target", entry.target},
          {"result", entry.result},
          {"obsRequestId", entry.request}};
}

void CommandDiagnostics::expire(std::uint64_t now, std::chrono::steady_clock::time_point observed)
{
  if (!entries_.empty() && now < entries_.back().unix_ms)
    entries_.clear();
  while (!entries_.empty() &&
         ((now >= entries_.front().unix_ms && now - entries_.front().unix_ms >= retention_ms) ||
          observed - entries_.front().observed >= std::chrono::hours(24)))
    entries_.pop_front();
}

void CommandDiagnostics::record(std::string_view device,
                                std::string_view action,
                                std::string_view target,
                                std::string_view result,
                                std::string_view request) noexcept
{
  try
  {
    Entry entry{now_(),
                std::chrono::steady_clock::now(),
                bounded(device),
                bounded(action),
                bounded(target, 1024),
                bounded(result),
                bounded(request)};
    {
      // Diagnostic contention may lose evidence, but must never wait behind another observer.
      std::unique_lock lock(mutex_, std::try_to_lock);
      if (!lock.owns_lock())
      {
        ++dropped_;
        return;
      }
      expire(entry.unix_ms, entry.observed);
      if (entries_.size() == maximum_entries)
        entries_.pop_front();
      entries_.push_back(entry);
    }
    if (recorder_ && recorder_->enabled())
      recorder_->record("command", "outcome", value(entry));
  }
  catch (...)
  {
    // This is the diagnostic producer boundary. Expose loss without invoking another logger.
    ++dropped_;
  }
}

json CommandDiagnostics::recent(std::size_t limit)
{
  const auto now = now_();
  const auto observed = std::chrono::steady_clock::now();
  std::lock_guard lock(mutex_);
  expire(now, observed);
  json result = json::array();
  for (auto entry = entries_.rbegin(); entry != entries_.rend() && result.size() < limit; ++entry)
    result.push_back(value(*entry));
  return result;
}

std::uint64_t CommandDiagnostics::dropped() const noexcept
{
  return dropped_.load(std::memory_order_relaxed);
}
} // namespace studio_remote
