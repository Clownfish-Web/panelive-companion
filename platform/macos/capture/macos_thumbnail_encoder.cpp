#include "macos_thumbnail_encoder.hpp"

#include <studio_remote/media/thumbnail_encoding.hpp>

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <memory>
#include <type_traits>

namespace studio_remote::macos
{
namespace
{
using Provider =
    std::unique_ptr<std::remove_pointer_t<CGDataProviderRef>, decltype(&CGDataProviderRelease)>;
using ColorSpace =
    std::unique_ptr<std::remove_pointer_t<CGColorSpaceRef>, decltype(&CGColorSpaceRelease)>;
using Image = std::unique_ptr<std::remove_pointer_t<CGImageRef>, decltype(&CGImageRelease)>;
using Data = std::unique_ptr<std::remove_pointer_t<CFMutableDataRef>, decltype(&CFRelease)>;
using Destination =
    std::unique_ptr<std::remove_pointer_t<CGImageDestinationRef>, decltype(&CFRelease)>;

Image make_image(const RawThumbnail& raw)
{
  // The provider borrows raw pixels only during synchronous encoding. Alpha is ignored, as
  // in the Windows WIC BGR conversion, rather than compositing transparent pixels over black.
  Provider provider(
      CGDataProviderCreateWithData(nullptr, raw.bgra.data(), raw.bgra.size(), nullptr),
      CGDataProviderRelease);
  ColorSpace color(CGColorSpaceCreateWithName(kCGColorSpaceSRGB), CGColorSpaceRelease);
  if (!provider || !color)
    throw std::runtime_error("Could not create image encoder");
  Image image(CGImageCreate(raw.width,
                            raw.height,
                            8,
                            32,
                            raw.stride,
                            color.get(),
                            static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little) |
                                static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipFirst),
                            provider.get(),
                            nullptr,
                            false,
                            kCGRenderingIntentDefault),
              CGImageRelease);
  if (!image)
    throw std::runtime_error("Could not create JPEG frame");
  return image;
}
} // namespace

EncodedThumbnail MacosThumbnailEncoder::encode(const RawThumbnail& raw)
{
  validate_raw_thumbnail(raw);
  auto image = make_image(raw);
  Data output(CFDataCreateMutable(kCFAllocatorDefault, 0), CFRelease);
  if (!output)
    throw std::runtime_error("Could not create image stream");
  Destination destination(
      CGImageDestinationCreateWithData(output.get(), CFSTR("public.jpeg"), 1, nullptr), CFRelease);
  if (!destination)
    throw std::runtime_error("Could not create JPEG encoder");
  // Like Windows, use the platform JPEG encoder's default quality.
  CGImageDestinationAddImage(destination.get(), image.get(), nullptr);
  if (!CGImageDestinationFinalize(destination.get()))
    throw std::runtime_error("Could not commit JPEG image");
  const auto size = CFDataGetLength(output.get());
  const auto* bytes = CFDataGetBytePtr(output.get());
  if (size <= 0 || !bytes)
    throw std::runtime_error("Encoded JPEG image is empty");
  return {"image/jpeg", std::vector<std::uint8_t>(bytes, bytes + size)};
}
} // namespace studio_remote::macos
