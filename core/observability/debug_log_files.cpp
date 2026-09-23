#include "debug_log_files.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace studio_remote
{
namespace
{
constexpr std::string_view prefix = "companion-endurance-";

std::optional<std::uint64_t> oldest_timestamp(std::string_view name)
{
  name.remove_prefix(prefix.size());
  const auto separator = name.find('-');
  if (separator == std::string_view::npos)
    return std::nullopt;
  const auto timestamp = name.substr(0, separator);
  // Current files use epoch milliseconds, not the retired local calendar naming format.
  if (timestamp.size() < 12)
    return std::nullopt;
  std::uint64_t result = 0;
  const auto parsed =
      std::from_chars(timestamp.data(), timestamp.data() + timestamp.size(), result);
  return parsed.ec == std::errc{} && parsed.ptr == timestamp.data() + timestamp.size()
             ? std::optional(result)
             : std::nullopt;
}
} // namespace

DebugLogFiles::DebugLogFiles(std::filesystem::path directory, DebugLogFileLimits limits)
    : directory_(std::move(directory)), limits_(limits)
{
  if (!limits_.maximum_files || !limits_.segment_bytes ||
      limits_.segment_bytes > limits_.maximum_bytes || limits_.retention.count() <= 0 ||
      limits_.segment_age.count() <= 0)
    throw std::invalid_argument("Invalid debug log storage limits");
}

void DebugLogFiles::discover()
{
  if (discovered_)
    return;
  if (!std::filesystem::exists(directory_))
  {
    discovered_ = true;
    return;
  }
  std::deque<Segment> found;
  std::uint64_t bytes = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory_))
  {
    const auto name = entry.path().filename().string();
    if (!name.starts_with(prefix) || !name.ends_with(".jsonl") ||
        !std::filesystem::is_regular_file(entry.symlink_status()))
      continue;
    const auto oldest = oldest_timestamp(name);
    if (!oldest)
    {
      // Superseded unmanaged diagnostic files have no trustworthy record-age bound.
      std::filesystem::remove(entry.path());
      continue;
    }
    const auto size = entry.file_size();
    found.push_back({entry.path(), *oldest, size});
    bytes += size;
    // A directory left by an older build may contain arbitrarily many segments. Bound discovery
    // itself, rather than allocating an unbounded list and applying retention afterwards.
    if (found.size() > limits_.maximum_files || bytes > limits_.maximum_bytes)
    {
      std::ranges::sort(found, {}, &Segment::oldest_ms);
      while (!found.empty() &&
             (found.size() > limits_.maximum_files || bytes > limits_.maximum_bytes))
      {
        std::filesystem::remove(found.front().path);
        bytes -= found.front().bytes;
        found.pop_front();
      }
    }
  }
  std::ranges::sort(found, {}, &Segment::oldest_ms);
  segments_ = std::move(found);
  retained_bytes_ = bytes;
  discovered_ = true;
}

void DebugLogFiles::remove_oldest()
{
  const auto& segment = segments_.front();
  if (segment.path == current_path_ && stream_.is_open())
    close();
  std::filesystem::remove(segment.path);
  retained_bytes_ -= segment.bytes;
  segments_.pop_front();
}

void DebugLogFiles::maintain(std::uint64_t now_ms)
{
  discover();
  if (previous_now_ms_ && now_ms < previous_now_ms_)
    clear();
  previous_now_ms_ = now_ms;
  // Future-dated files cannot prove their age after a wall-clock correction/restart.
  const bool future = !segments_.empty() && segments_.back().oldest_ms > now_ms;
  if (future)
    clear();
  while (!segments_.empty() &&
         (now_ms - segments_.front().oldest_ms >=
              static_cast<std::uint64_t>(limits_.retention.count()) ||
          retained_bytes_ > limits_.maximum_bytes || segments_.size() > limits_.maximum_files))
    remove_oldest();
}

void DebugLogFiles::open_segment(std::uint64_t now_ms)
{
  // The recorder rejects events queued for a minute. This conservative lower bound also covers
  // producer timestamps taken just before this segment opens and remains valid after restart.
  const auto oldest_ms = now_ms > 60'000 ? now_ms - 60'000 : 0;
  std::filesystem::create_directories(directory_);
  for (std::size_t suffix = 0; suffix <= limits_.maximum_files; ++suffix)
  {
    auto path = directory_ / (std::string(prefix) + std::to_string(oldest_ms) + "-" +
                              std::to_string(suffix) + ".jsonl");
    if (std::filesystem::exists(path))
      continue;
    stream_.clear();
    stream_.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!stream_)
      throw std::runtime_error("Could not create the debug log file");
    current_path_ = path;
    segments_.push_back({std::move(path), oldest_ms, 0});
    return;
  }
  throw std::runtime_error("Could not allocate a debug log filename");
}

void DebugLogFiles::append(std::string_view line, std::uint64_t now_ms)
{
  const auto bytes = line.size() + 1;
  if (bytes > limits_.segment_bytes)
    throw std::runtime_error("Debug record exceeds the segment budget");
  maintain(now_ms);
  if (stream_.is_open() && (segments_.back().bytes + bytes > limits_.segment_bytes ||
                            now_ms - segments_.back().oldest_ms >=
                                static_cast<std::uint64_t>(limits_.segment_age.count())))
    close();
  while (!segments_.empty() && (retained_bytes_ + bytes > limits_.maximum_bytes ||
                                (!stream_.is_open() && segments_.size() >= limits_.maximum_files)))
    remove_oldest();
  if (!stream_.is_open())
    open_segment(now_ms);
  segments_.back().bytes += bytes;
  retained_bytes_ += bytes;
  stream_ << line << '\n';
  if (!stream_)
    throw std::runtime_error("Writing debug diagnostics failed");
}

void DebugLogFiles::flush()
{
  if (!stream_.is_open())
    return;
  stream_.flush();
  if (!stream_)
    throw std::runtime_error("Flushing debug diagnostics failed");
}

void DebugLogFiles::close()
{
  if (!stream_.is_open())
    return;
  stream_.flush();
  const bool failed = !stream_;
  stream_.close();
  if (failed || stream_.fail())
    throw std::runtime_error("Closing debug diagnostics failed");
}

void DebugLogFiles::clear()
{
  discover();
  close();
  while (!segments_.empty())
    remove_oldest();
  current_path_.clear();
}

std::uint64_t DebugLogFiles::retained_bytes() const noexcept
{
  return retained_bytes_;
}

const std::filesystem::path& DebugLogFiles::current_path() const noexcept
{
  return current_path_;
}
} // namespace studio_remote
