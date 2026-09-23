#pragma once

#include <studio_remote/media/thumbnail.hpp>

#include <mutex>
#include <string>

namespace studio_remote::linux
{

class LinuxThumbnailCapture final : public IThumbnailCapture
{
public:
  explicit LinuxThumbnailCapture(std::string mapping_name);
  LinuxThumbnailCapture();
  RawThumbnail capture(const ThumbnailCaptureRequest& request) override;

private:
  std::string mapping_name_;
  std::mutex capture_mutex_;
};

} // namespace studio_remote::linux
