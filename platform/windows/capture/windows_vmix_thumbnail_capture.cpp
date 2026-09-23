#include "windows_vmix_thumbnail_capture.hpp"

#include <studio_remote/common/types.hpp>

#include <Windows.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace studio_remote::windows
{
namespace
{
template <typename Type> class ComPointer
{
public:
  ~ComPointer()
  {
    if (value_)
      value_->Release();
  }

  Type** put()
  {
    return &value_;
  }

  Type* operator->() const
  {
    return value_;
  }

private:
  Type* value_ = nullptr;
};

class ComApartment
{
public:
  ComApartment()
  {
    const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    initialized_ = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE)
      throw std::runtime_error("Could not initialize vMix thumbnail decoder");
  }

  ~ComApartment()
  {
    if (initialized_)
      CoUninitialize();
  }

private:
  bool initialized_ = false;
};

void require(HRESULT result, const char* message)
{
  if (FAILED(result))
    throw std::runtime_error(message);
}

std::uint64_t resource_hash(std::string_view value)
{
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto character : value)
  {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool file_readable(const std::filesystem::path& path)
{
  const HANDLE file = CreateFileW(path.c_str(),
                                  GENERIC_READ,
                                  FILE_SHARE_READ,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  CloseHandle(file);
  return true;
}
} // namespace

WindowsVmixThumbnailCapture::WindowsVmixThumbnailCapture(std::filesystem::path temporary_directory,
                                                         SnapshotRequest request_snapshot,
                                                         bool available,
                                                         VmixThumbnailCaptureLimits limits)
    : temporary_directory_(std::filesystem::absolute(std::move(temporary_directory))),
      request_snapshot_(std::move(request_snapshot)), available_(available), limits_(limits)
{
  if (temporary_directory_.empty() || !request_snapshot_)
    throw std::invalid_argument("vMix thumbnail capture configuration is incomplete");
  if (limits_.file_poll_interval < std::chrono::milliseconds(0) ||
      limits_.capture_timeout <= std::chrono::milliseconds(0))
    throw std::invalid_argument("vMix thumbnail capture limits are invalid");
  std::filesystem::create_directories(temporary_directory_);
}

bool WindowsVmixThumbnailCapture::available() const noexcept
{
  return available_.load(std::memory_order_relaxed);
}

void WindowsVmixThumbnailCapture::set_available(bool available) noexcept
{
  available_.store(available, std::memory_order_relaxed);
}

RawThumbnail WindowsVmixThumbnailCapture::capture(const ThumbnailCaptureRequest& request)
{
  if (!available())
    throw ThumbnailCaptureError("co_location_required",
                                "vMix thumbnails require Companion and vMix on the same computer");
  if (request.resource_id.empty() || request.maximum_width == 0 || request.maximum_height == 0)
    throw std::invalid_argument("Invalid vMix thumbnail request");

  const auto path = snapshot_path(request.resource_id);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  if (std::filesystem::exists(path))
    throw ThumbnailCaptureError("temporary_file_unavailable",
                                "Could not replace the previous vMix snapshot file");
  request_snapshot_(request.resource_id, path);
  wait_for_complete_file(path);
  return decode(path, request.maximum_width, request.maximum_height);
}

std::filesystem::path WindowsVmixThumbnailCapture::snapshot_path(std::string_view resource_id) const
{
  std::array<char, 32> name{};
  std::snprintf(name.data(), name.size(), "input-%016llx.jpg", resource_hash(resource_id));
  return temporary_directory_ / name.data();
}

void WindowsVmixThumbnailCapture::wait_for_complete_file(const std::filesystem::path& path) const
{
  const auto deadline = std::chrono::steady_clock::now() + limits_.capture_timeout;
  std::uintmax_t previous_size = 0;
  unsigned int stable_observations = 0;
  while (std::chrono::steady_clock::now() < deadline)
  {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (!error && size > 0 && size == previous_size && file_readable(path))
    {
      if (++stable_observations >= 2)
        return;
    }
    else
    {
      stable_observations = 0;
    }
    previous_size = error ? 0 : size;
    if (limits_.file_poll_interval > std::chrono::milliseconds(0))
      std::this_thread::sleep_for(limits_.file_poll_interval);
    else
      std::this_thread::yield();
  }
  throw ThumbnailCaptureError("render_timeout", "vMix snapshot file did not become readable");
}

RawThumbnail WindowsVmixThumbnailCapture::decode(const std::filesystem::path& path,
                                                 std::uint32_t maximum_width,
                                                 std::uint32_t maximum_height) const
{
  ComApartment apartment;
  ComPointer<IWICImagingFactory> factory;
  ComPointer<IWICBitmapDecoder> decoder;
  ComPointer<IWICBitmapFrameDecode> frame;
  ComPointer<IWICBitmapScaler> scaler;
  ComPointer<IWICFormatConverter> converter;
  require(CoCreateInstance(
              CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())),
          "Could not create vMix thumbnail decoder");
  require(factory->CreateDecoderFromFilename(
              path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.put()),
          "Could not open vMix snapshot image");
  require(decoder->GetFrame(0, frame.put()), "Could not read vMix snapshot frame");

  UINT source_width = 0;
  UINT source_height = 0;
  require(frame->GetSize(&source_width, &source_height), "Could not size vMix snapshot frame");
  if (source_width == 0 || source_height == 0)
    throw std::runtime_error("vMix snapshot image is empty");
  const auto scale = std::min({1.0,
                               static_cast<double>(maximum_width) / source_width,
                               static_cast<double>(maximum_height) / source_height});
  const auto width = std::max(1U, static_cast<UINT>(source_width * scale));
  const auto height = std::max(1U, static_cast<UINT>(source_height * scale));

  require(factory->CreateBitmapScaler(scaler.put()), "Could not create vMix thumbnail scaler");
  require(scaler->Initialize(frame.operator->(), width, height, WICBitmapInterpolationModeFant),
          "Could not scale vMix snapshot image");
  require(factory->CreateFormatConverter(converter.put()),
          "Could not create vMix thumbnail converter");
  require(converter->Initialize(scaler.operator->(),
                                GUID_WICPixelFormat32bppBGRA,
                                WICBitmapDitherTypeNone,
                                nullptr,
                                0.0,
                                WICBitmapPaletteTypeCustom),
          "Could not convert vMix snapshot image");

  RawThumbnail result;
  result.width = width;
  result.height = height;
  result.stride = width * 4;
  result.observed_at_ns = monotonic_ns();
  result.bgra.resize(static_cast<std::size_t>(result.stride) * result.height);
  require(converter->CopyPixels(
              nullptr, result.stride, static_cast<UINT>(result.bgra.size()), result.bgra.data()),
          "Could not copy vMix thumbnail pixels");
  return result;
}

bool is_local_vmix_host(std::string_view host)
{
  return host == "127.0.0.1" || host == "localhost" || host == "::1" || host == "[::1]";
}

} // namespace studio_remote::windows
