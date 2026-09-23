#pragma once

#include <studio_remote/media/thumbnail.hpp>

namespace studio_remote::linux
{

class LinuxThumbnailEncoder final : public IThumbnailEncoder
{
public:
  EncodedThumbnail encode(const RawThumbnail& image) override;
};

} // namespace studio_remote::linux
