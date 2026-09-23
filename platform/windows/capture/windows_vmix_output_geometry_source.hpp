#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <studio_remote/vmix/vmix_output_geometry.hpp>

namespace studio_remote::windows
{

class WindowsVmixOutputGeometrySource
{
public:
  explicit WindowsVmixOutputGeometrySource(bool available);

  std::optional<vmix::OutputGeometry> current();

private:
  std::filesystem::path locate_settings() const;
  std::optional<vmix::OutputGeometry> read_settings(const std::filesystem::path& path);

  bool available_ = false;
  std::mutex mutex_;
  std::filesystem::path settings_path_;
  std::filesystem::file_time_type cached_write_time_{};
  std::uintmax_t cached_size_ = 0;
  std::optional<vmix::OutputGeometry> cached_geometry_;
  std::chrono::steady_clock::time_point next_check_{};
};

} // namespace studio_remote::windows
