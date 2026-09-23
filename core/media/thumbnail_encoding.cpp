#include <studio_remote/media/thumbnail_encoding.hpp>

#include <cstdint>
#include <stdexcept>

namespace studio_remote
{
void validate_raw_thumbnail(const RawThumbnail& image)
{
  if (image.width == 0 || image.height == 0 ||
      image.stride < static_cast<std::uint64_t>(image.width) * 4 ||
      image.bgra.size() < static_cast<std::uint64_t>(image.stride) * image.height)
    throw std::invalid_argument("Invalid raw thumbnail");
}
} // namespace studio_remote
