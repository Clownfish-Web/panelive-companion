#include "windows_thumbnail_encoder.hpp"

#include <studio_remote/media/thumbnail_encoding.hpp>

#include <Windows.h>
#include <objidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace studio_remote::windows
{
namespace
{
using Microsoft::WRL::ComPtr;

void require(HRESULT result, const char* message)
{
  if (FAILED(result))
    throw std::runtime_error(message);
}

class ComApartment
{
public:
  ComApartment()
  {
    const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    initialized_ = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE)
      throw std::runtime_error("Could not initialize image encoder");
  }
  ~ComApartment()
  {
    if (initialized_)
      CoUninitialize();
  }
  ComApartment(const ComApartment&) = delete;
  ComApartment& operator=(const ComApartment&) = delete;

private:
  bool initialized_ = false;
};

void write_pixels(IWICBitmapFrameEncode& frame, const RawThumbnail& image)
{
  // Raw validation has already proved width * 4 fits the uint32 stride. WIC additionally
  // requires the entire converted buffer size to fit its UINT argument.
  const auto target_stride = image.width * 3;
  const auto target_size = static_cast<std::uint64_t>(target_stride) * image.height;
  if (target_size > std::numeric_limits<UINT>::max())
    throw std::invalid_argument("Invalid raw thumbnail");
  std::vector<std::uint8_t> bgr(static_cast<std::size_t>(target_size));
  for (std::uint32_t row = 0; row < image.height; ++row)
  {
    for (std::uint32_t column = 0; column < image.width; ++column)
    {
      const auto source = static_cast<std::size_t>(row) * image.stride + column * 4;
      const auto target = static_cast<std::size_t>(row) * target_stride + column * 3;
      bgr[target] = image.bgra[source];
      bgr[target + 1] = image.bgra[source + 1];
      bgr[target + 2] = image.bgra[source + 2];
    }
  }
  require(frame.WritePixels(image.height, target_stride, static_cast<UINT>(bgr.size()), bgr.data()),
          "Could not encode JPEG pixels");
}

EncodedThumbnail read_image(IStream& stream)
{
  // The stream's logical length can be smaller than its rounded HGLOBAL allocation. Read
  // through IStream so neither allocation padding nor a borrowed memory lock escapes.
  STATSTG statistics{};
  require(stream.Stat(&statistics, STATFLAG_NONAME), "Could not read JPEG image");
  const auto size = statistics.cbSize.QuadPart;
  if (size == 0)
    throw std::runtime_error("Encoded JPEG image is empty");
  if (size > std::numeric_limits<ULONG>::max())
    throw std::runtime_error("Could not read JPEG image");
  require(stream.Seek({}, STREAM_SEEK_SET, nullptr), "Could not read JPEG image");
  EncodedThumbnail result{"image/jpeg", std::vector<std::uint8_t>(static_cast<std::size_t>(size))};
  ULONG bytes_read = 0;
  require(stream.Read(result.bytes.data(), static_cast<ULONG>(size), &bytes_read),
          "Could not read JPEG image");
  if (bytes_read != size)
    throw std::runtime_error("Could not read JPEG image");
  return result;
}
} // namespace

EncodedThumbnail WindowsThumbnailEncoder::encode(const RawThumbnail& image)
{
  validate_raw_thumbnail(image);

  ComApartment apartment;

  ComPtr<IWICImagingFactory> factory;
  ComPtr<IStream> stream;
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> frame;
  require(CoCreateInstance(
              CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
          "Could not create image encoder");
  require(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "Could not create image stream");
  require(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder),
          "Could not create JPEG encoder");
  require(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache),
          "Could not initialize JPEG encoder");
  require(encoder->CreateNewFrame(&frame, nullptr), "Could not create JPEG frame");
  require(frame->Initialize(nullptr), "Could not initialize JPEG frame");
  require(frame->SetSize(image.width, image.height), "Could not size JPEG frame");
  WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
  require(frame->SetPixelFormat(&format), "Could not set JPEG pixel format");
  if (format != GUID_WICPixelFormat24bppBGR)
    throw std::runtime_error("JPEG pixel format is unavailable");

  write_pixels(*frame.Get(), image);
  require(frame->Commit(), "Could not commit JPEG frame");
  require(encoder->Commit(), "Could not commit JPEG image");

  return read_image(*stream.Get());
}

} // namespace studio_remote::windows
