#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <studio_remote/vmix/vmix_input_category_source.hpp>

namespace studio_remote::windows
{

class WindowsVmixInputCategorySource final : public vmix::IVmixInputCategorySource
{
public:
  explicit WindowsVmixInputCategorySource(bool available);

  bool available() const override;
  std::optional<std::string> read_current_preset() override;
  void set_available(bool available);

private:
  std::atomic<bool> available_;
  std::mutex mutex_;
  std::string cached_preset_;
  std::filesystem::file_time_type cached_write_time_{};
  std::uintmax_t cached_size_ = 0;
};

} // namespace studio_remote::windows
