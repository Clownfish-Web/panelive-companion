#include "macos_thumbnail_capture.hpp"

#include "../ipc/writable_shared_memory.hpp"

#include <studio_remote/ipc/source_thumbnail_mapping.hpp>
#include <studio_remote/ipc/source_thumbnail_protocol.hpp>

#include <thread>
#include <utility>

namespace studio_remote::macos
{
MacosThumbnailCapture::MacosThumbnailCapture(std::string mapping_name,
                                             std::chrono::milliseconds timeout)
    : mapping_name_(std::move(mapping_name)), timeout_(timeout)
{
  ipc::validate_source_thumbnail_timeout(timeout);
}

ipc::CapturedThumbnail MacosThumbnailCapture::capture(std::string_view source_id,
                                                      std::string_view source_name,
                                                      std::uint32_t maximum_width,
                                                      std::uint32_t maximum_height,
                                                      std::chrono::milliseconds timeout)
{
  const ThumbnailCaptureRequest request{ThumbnailTarget::source,
                                        std::string(source_id),
                                        std::string(source_name),
                                        maximum_width,
                                        maximum_height};
  auto memory = WritableSharedMemory::open(mapping_name_, sizeof(ipc::SourceThumbnailMapping));
  if (!memory)
    throw std::runtime_error("OBS source thumbnail IPC is unavailable");
  auto& mapping = *reinterpret_cast<ipc::SourceThumbnailMapping*>(memory->bytes().data());
  return ipc::capture_source_thumbnail_raw(mapping,
                                           request,
                                           timeout,
                                           {[] { return std::chrono::steady_clock::now(); },
                                            [](std::chrono::milliseconds duration)
                                            { std::this_thread::sleep_for(duration); }});
}

RawThumbnail MacosThumbnailCapture::capture(const ThumbnailCaptureRequest& request)
{
  ipc::CapturedThumbnail result;
  try
  {
    result = capture(request.resource_id,
                     request.resource_name,
                     request.maximum_width,
                     request.maximum_height,
                     timeout_);
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
} // namespace studio_remote::macos
