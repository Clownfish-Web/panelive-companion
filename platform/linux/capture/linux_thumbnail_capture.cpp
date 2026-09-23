#include "linux_thumbnail_capture.hpp"

#include <studio_remote/ipc/source_thumbnail_mapping.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace studio_remote::linux
{
namespace
{
std::atomic<std::uint64_t> next_sequence{
    static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
    (static_cast<std::uint64_t>(getpid()) << 24U)};

void copy_text(char* target, std::size_t capacity, std::string_view value)
{
  const auto size = std::min(capacity - 1, value.size());
  std::memcpy(target, value.data(), size);
  target[size] = '\0';
}

class MappedThumbnailMailbox
{
public:
  explicit MappedThumbnailMailbox(const std::string& name)
  {
    descriptor_ = shm_open(name.c_str(), O_RDWR | O_CLOEXEC, 0);
    struct stat status
    {
    };
    if (descriptor_ < 0 || fstat(descriptor_, &status) != 0 || status.st_uid != geteuid() ||
        (status.st_mode & 0077) != 0 ||
        status.st_size < static_cast<off_t>(sizeof(ipc::SourceThumbnailMapping)))
    {
      if (descriptor_ >= 0)
      {
        close(descriptor_);
        descriptor_ = -1;
      }
      throw ThumbnailCaptureError("helper_unavailable",
                                  "OBS source thumbnail IPC is unavailable");
    }
    auto* address = mmap(nullptr,
                         sizeof(ipc::SourceThumbnailMapping),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED,
                         descriptor_,
                         0);
    if (address == MAP_FAILED)
    {
      close(descriptor_);
      descriptor_ = -1;
      throw ThumbnailCaptureError("helper_unavailable",
                                  "Could not map OBS source thumbnail IPC");
    }
    mapping_ = static_cast<ipc::SourceThumbnailMapping*>(address);
  }

  ~MappedThumbnailMailbox()
  {
    if (mapping_)
      munmap(mapping_, sizeof(ipc::SourceThumbnailMapping));
    if (descriptor_ >= 0)
      close(descriptor_);
  }

  ipc::SourceThumbnailMapping* operator->() const
  {
    return mapping_;
  }

private:
  int descriptor_ = -1;
  ipc::SourceThumbnailMapping* mapping_ = nullptr;
};
} // namespace

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
  if (request.resource_id.size() > 128 || request.resource_name.size() > 255 ||
      request.maximum_width == 0 || request.maximum_height == 0 ||
      request.maximum_width > ipc::maximum_thumbnail_width ||
      request.maximum_height > ipc::maximum_thumbnail_height)
  {
    throw std::invalid_argument("Invalid source thumbnail request");
  }

  MappedThumbnailMailbox mapping(mapping_name_);
  if (mapping->magic != ipc::source_thumbnail_magic ||
      mapping->version != ipc::source_thumbnail_version ||
      mapping->structure_size != sizeof(ipc::SourceThumbnailMapping))
  {
    throw ThumbnailCaptureError("helper_unavailable", "OBS source thumbnail IPC version mismatch");
  }

  const auto sequence = next_sequence.fetch_add(1, std::memory_order_relaxed);
  copy_text(mapping->source_id, sizeof(mapping->source_id), request.resource_id);
  copy_text(mapping->source_name, sizeof(mapping->source_name), request.resource_name);
  mapping->maximum_width = request.maximum_width;
  mapping->maximum_height = request.maximum_height;
  mapping->request_sequence.store(sequence, std::memory_order_release);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (mapping->response_sequence.load(std::memory_order_acquire) != sequence)
  {
    if (std::chrono::steady_clock::now() >= deadline)
      throw ThumbnailCaptureError("render_timeout", "OBS thumbnail IPC timed out");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  const auto status = static_cast<ipc::ThumbnailStatus>(mapping->status);
  if (status == ipc::ThumbnailStatus::source_initialization_timeout)
    throw ThumbnailCaptureError("source_initialization_timeout",
                                "OBS source did not produce a ready frame in time");
  if (status == ipc::ThumbnailStatus::activation_required)
    throw ThumbnailCaptureError(
        "source_inactive",
        "OBS source requires activation to produce a frame; passive capture refused");
  if (status == ipc::ThumbnailStatus::source_unavailable)
    throw ThumbnailCaptureError("source_unavailable",
                                "OBS source exists but its backing capture target is unavailable");
  const auto data_size = mapping->data_size;
  if (status != ipc::ThumbnailStatus::success || data_size == 0 ||
      data_size > ipc::maximum_thumbnail_bytes)
  {
    throw ThumbnailCaptureError(request.target == ThumbnailTarget::scene ? "scene_not_renderable"
                                                                         : "source_not_renderable",
                                "OBS plugin could not render the requested thumbnail target");
  }
  std::vector<std::uint8_t> pixels(mapping->pixels, mapping->pixels + data_size);
  return {
      mapping->width, mapping->height, mapping->stride, mapping->captured_ns, std::move(pixels)};
}

} // namespace studio_remote::linux
