#pragma once

#include <studio_remote/media/thumbnail.hpp>

namespace studio_remote
{
/// Validate the owned strided BGRA image before acquiring native encoder resources. Row padding
/// and extra trailing bytes are allowed; alpha is ignored by the native JPEG encoders.
void validate_raw_thumbnail(const RawThumbnail& image);
} // namespace studio_remote
