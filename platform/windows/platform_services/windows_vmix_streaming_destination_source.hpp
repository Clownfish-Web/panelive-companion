#pragma once

#include <studio_remote/vmix/vmix_streaming_destination_source.hpp>

#include <atomic>
#include <filesystem>
#include <mutex>

namespace studio_remote::windows
{

class WindowsVmixStreamingDestinationSource final : public vmix::IVmixStreamingDestinationSource
{
public:
  explicit WindowsVmixStreamingDestinationSource(bool available,
                                                 std::filesystem::path settings_path = {},
                                                 std::filesystem::path local_app_data = {});

  bool available() const override;
  std::optional<std::vector<vmix::StreamingDestinationConfiguration>> read_destinations() override;
  void set_available(bool available);

private:
  std::atomic<bool> available_;
  std::filesystem::path settings_path_;
  std::filesystem::path local_app_data_;
  std::filesystem::path cached_path_;
  std::mutex mutex_;
  std::optional<std::vector<vmix::StreamingDestinationConfiguration>> cached_destinations_;
  std::filesystem::file_time_type cached_write_time_{};
  std::uintmax_t cached_size_ = 0;
};

} // namespace studio_remote::windows
