#include <studio_remote/ipc/source_thumbnail_protocol.hpp>

#include <studio_remote/ipc/source_thumbnail_mapping.hpp>

#include <cstring>
#include <stdexcept>
#include <utility>

namespace studio_remote::ipc
{
namespace
{
void validate_request(const ThumbnailCaptureRequest& request)
{
  if (request.resource_id.size() > 128 || request.resource_name.size() > 255 ||
      request.resource_id.find('\0') != std::string::npos ||
      request.resource_name.find('\0') != std::string::npos || request.maximum_width == 0 ||
      request.maximum_height == 0 || request.maximum_width > maximum_thumbnail_width ||
      request.maximum_height > maximum_thumbnail_height)
    throw std::invalid_argument("Invalid source thumbnail request");
}

[[noreturn]] void not_renderable(ThumbnailTarget target)
{
  throw ThumbnailCaptureError(target == ThumbnailTarget::scene ? "scene_not_renderable"
                                                               : "source_not_renderable",
                              "OBS plugin could not render the requested thumbnail target");
}

CapturedThumbnail read_response(const SourceThumbnailMapping& mapping,
                                const ThumbnailCaptureRequest& request)
{
  CapturedThumbnail result;
  result.status = static_cast<ThumbnailStatus>(mapping.status);
  result.width = mapping.width;
  result.height = mapping.height;
  result.stride = mapping.stride;
  const auto data_size = mapping.data_size;
  result.captured_ns = mapping.captured_ns;
  if (result.status == ThumbnailStatus::success && result.width != 0 && result.height != 0 &&
      result.width <= request.maximum_width && result.height <= request.maximum_height &&
      result.stride >= result.width * thumbnail_bytes_per_pixel &&
      data_size <= maximum_thumbnail_bytes &&
      static_cast<std::uint64_t>(result.stride) * result.height == data_size)
    result.bgra.assign(mapping.pixels, mapping.pixels + data_size);
  return result;
}
} // namespace

void validate_source_thumbnail_timeout(std::chrono::milliseconds timeout)
{
  if (timeout < std::chrono::milliseconds::zero() || timeout > std::chrono::seconds(3))
    throw std::invalid_argument("Invalid source thumbnail timeout");
}

CapturedThumbnail capture_source_thumbnail_raw(SourceThumbnailMapping& mapping,
                                               const ThumbnailCaptureRequest& request,
                                               std::chrono::milliseconds timeout,
                                               const SourceThumbnailTiming& timing)
{
  if (mapping.magic != source_thumbnail_magic || mapping.version != source_thumbnail_version ||
      mapping.structure_size != sizeof(SourceThumbnailMapping))
    throw std::runtime_error("OBS source thumbnail IPC version mismatch");
  validate_request(request);
  validate_source_thumbnail_timeout(timeout);
  if (!timing.now || !timing.pause)
    throw std::invalid_argument("Invalid source thumbnail timing");

  auto sequence = mapping.request_sequence.load(std::memory_order_acquire);
  const auto previous_response = mapping.response_sequence.load(std::memory_order_acquire);
  do
  {
    ++sequence;
  } while (sequence == 0 || sequence == previous_response);
  std::memcpy(mapping.source_id, request.resource_id.c_str(), request.resource_id.size() + 1);
  std::memcpy(mapping.source_name, request.resource_name.c_str(), request.resource_name.size() + 1);
  mapping.maximum_width = request.maximum_width;
  mapping.maximum_height = request.maximum_height;
  mapping.request_sequence.store(sequence, std::memory_order_release);

  const auto deadline = timing.now() + timeout;
  while (mapping.response_sequence.load(std::memory_order_acquire) != sequence)
  {
    if (timing.now() >= deadline)
      throw ThumbnailCaptureError("render_timeout", "OBS thumbnail IPC timed out");
    timing.pause(std::chrono::milliseconds(5));
  }
  return read_response(mapping, request);
}
RawThumbnail project_source_thumbnail(CapturedThumbnail result,
                                      const ThumbnailCaptureRequest& request)
{
  if (result.status == ThumbnailStatus::source_initialization_timeout)
    throw ThumbnailCaptureError("source_initialization_timeout",
                                "OBS source did not produce a ready frame in time");
  if (result.status == ThumbnailStatus::activation_required)
    throw ThumbnailCaptureError(
        "source_inactive",
        "OBS source requires activation to produce a frame; passive capture refused");
  if (result.status == ThumbnailStatus::source_unavailable)
    throw ThumbnailCaptureError("source_unavailable",
                                "OBS source exists but its backing capture target is unavailable");
  if (result.status != ThumbnailStatus::success || result.bgra.empty())
    not_renderable(request.target);
  return {result.width, result.height, result.stride, result.captured_ns, std::move(result.bgra)};
}

} // namespace studio_remote::ipc
