#include "linux_thumbnail_capture.hpp"

#include "linux_thumbnail_mailbox.hpp"
#include <studio_remote/ipc/source_thumbnail_protocol.hpp>

#include <thread>
#include <utility>

namespace studio_remote::linux
{
LinuxThumbnailCapture::LinuxThumbnailCapture()
    : LinuxThumbnailCapture(ipc::source_thumbnail_shared_memory_name)
{
}
LinuxThumbnailCapture::LinuxThumbnailCapture(std::string mapping_name)
    : mapping_name_(std::move(mapping_name))
{
}

RawThumbnail LinuxThumbnailCapture::capture(const ThumbnailCaptureRequest& request)
{
  std::lock_guard lock(capture_mutex_);
  ipc::CapturedThumbnail result;
  try
  {
    auto memory = LinuxThumbnailMailbox::open(mapping_name_);
    result = ipc::capture_source_thumbnail_raw(memory.mapping(),
                                               request,
                                               std::chrono::seconds(3),
                                               {[] { return std::chrono::steady_clock::now(); },
                                                [](std::chrono::milliseconds duration)
                                                { std::this_thread::sleep_for(duration); }});
  }
  catch (const ThumbnailCaptureError&)
  {
    throw;
  }
  catch (const std::runtime_error& error)
  {
    throw ThumbnailCaptureError("helper_unavailable", error.what());
  }
  return ipc::project_source_thumbnail(std::move(result), request);
}
} // namespace studio_remote::linux
