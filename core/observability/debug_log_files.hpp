#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace studio_remote
{
struct DebugLogFileLimits
{
  std::uint64_t maximum_bytes = 64 * 1024 * 1024;
  std::uint64_t segment_bytes = 1024 * 1024;
  std::size_t maximum_files = 512;
  std::chrono::milliseconds retention = std::chrono::hours(24);
  std::chrono::milliseconds segment_age = std::chrono::minutes(5);
};

/// Single diagnostic-worker ownership. No producer or UI thread accesses the filesystem.
/// Names encode the oldest possible record time; cleanup never relies on a last-write timestamp.
class DebugLogFiles final
{
public:
  explicit DebugLogFiles(std::filesystem::path directory, DebugLogFileLimits limits = {});
  void maintain(std::uint64_t now_ms);
  void append(std::string_view line, std::uint64_t now_ms);
  void flush();
  void close();
  void clear();
  std::uint64_t retained_bytes() const noexcept;
  const std::filesystem::path& current_path() const noexcept;

private:
  struct Segment
  {
    std::filesystem::path path;
    std::uint64_t oldest_ms;
    std::uint64_t bytes;
  };
  void discover();
  void remove_oldest();
  void open_segment(std::uint64_t now_ms);

  const std::filesystem::path directory_;
  const DebugLogFileLimits limits_;
  std::deque<Segment> segments_;
  std::ofstream stream_;
  std::filesystem::path current_path_;
  std::uint64_t retained_bytes_ = 0;
  std::uint64_t previous_now_ms_ = 0;
  bool discovered_ = false;
};
} // namespace studio_remote
