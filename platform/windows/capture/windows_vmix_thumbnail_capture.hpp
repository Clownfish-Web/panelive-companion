#pragma once

#include <studio_remote/media/thumbnail.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>

namespace studio_remote::windows
{

struct VmixThumbnailCaptureLimits
{
  std::chrono::milliseconds file_poll_interval = std::chrono::milliseconds(25);
  std::chrono::milliseconds capture_timeout = std::chrono::milliseconds(2'500);
};

class WindowsVmixThumbnailCapture final : public IThumbnailCapture
{
public:
  using SnapshotRequest = std::function<void(std::string_view, const std::filesystem::path&)>;

  WindowsVmixThumbnailCapture(std::filesystem::path temporary_directory,
                              SnapshotRequest request_snapshot,
                              bool available,
                              VmixThumbnailCaptureLimits limits = {});

  bool available() const noexcept override;
  void set_available(bool available) noexcept;
  RawThumbnail capture(const ThumbnailCaptureRequest& request) override;

private:
  std::filesystem::path snapshot_path(std::string_view resource_id) const;
  void wait_for_complete_file(const std::filesystem::path& path) const;
  RawThumbnail decode(const std::filesystem::path& path,
                      std::uint32_t maximum_width,
                      std::uint32_t maximum_height) const;

  std::filesystem::path temporary_directory_;
  SnapshotRequest request_snapshot_;
  std::atomic<bool> available_;
  VmixThumbnailCaptureLimits limits_;
};

bool is_local_vmix_host(std::string_view host);

} // namespace studio_remote::windows
