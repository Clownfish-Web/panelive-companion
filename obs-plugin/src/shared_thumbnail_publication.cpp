#include "shared_thumbnail_publication.hpp"

#include <cstring>

namespace studio_remote::obs_plugin
{
void publish_thumbnail_response(ipc::SourceThumbnailMapping& mapping,
                                std::uint64_t sequence,
                                ipc::ThumbnailStatus status,
                                std::span<const std::uint8_t> pixels,
                                std::uint32_t width,
                                std::uint32_t height,
                                std::uint32_t stride,
                                std::uint64_t captured_ns)
{
  const auto extent = static_cast<std::uint64_t>(stride) * height;
  const bool valid_pixels = width != 0 && height != 0 && width <= ipc::maximum_thumbnail_width &&
                            height <= ipc::maximum_thumbnail_height &&
                            stride >= width * ipc::thumbnail_bytes_per_pixel &&
                            extent <= ipc::maximum_thumbnail_bytes && extent <= pixels.size();
  if (status == ipc::ThumbnailStatus::success && !valid_pixels)
    status = ipc::ThumbnailStatus::render_failed;
  const auto data_size =
      status == ipc::ThumbnailStatus::success ? static_cast<std::uint32_t>(extent) : 0U;
  if (data_size == 0)
    width = height = stride = 0;

  mapping.status = static_cast<std::uint32_t>(status);
  mapping.width = width;
  mapping.height = height;
  mapping.stride = stride;
  mapping.data_size = data_size;
  mapping.captured_ns = captured_ns;
  // The control mapping is writable by consumers; never read a shared length back for memcpy.
  if (data_size != 0)
    std::memcpy(mapping.pixels, pixels.data(), data_size);
  mapping.response_sequence.store(sequence, std::memory_order_release);
}
} // namespace studio_remote::obs_plugin
