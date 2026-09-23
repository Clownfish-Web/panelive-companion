#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote
{
using FrameId = std::uint64_t;
using MonotonicTimestamp = std::chrono::nanoseconds;
enum class PixelFormat
{
  bgra8,
  rgba8,
  nv12,
  i420
};
enum class ColorSpace
{
  srgb,
  rec709_limited,
  rec709_full
};

class FrameStorage
{
public:
  FrameStorage() = default;
  FrameStorage(std::shared_ptr<void> owner, std::string kind)
      : owner_(std::move(owner)), kind_(std::move(kind))
  {
  }
  const std::string& kind() const noexcept
  {
    return kind_;
  }
  explicit operator bool() const noexcept
  {
    return static_cast<bool>(owner_);
  }

private:
  std::shared_ptr<void> owner_;
  std::string kind_;
};

struct MediaFrame
{
  FrameId id = 0;
  MonotonicTimestamp captured_at{};
  PixelFormat pixel_format = PixelFormat::bgra8;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  ColorSpace color_space = ColorSpace::srgb;
  FrameStorage storage;
};

struct CaptureTarget
{
  std::string id;
  std::string display_name;
  std::string surface;
};
class ICaptureProvider
{
public:
  using FrameCallback = std::function<void(std::string_view, MediaFrame)>;
  virtual ~ICaptureProvider() = default;
  virtual std::vector<CaptureTarget> enumerate() = 0;
  virtual void
  start(std::string_view target_id, std::string_view track, FrameCallback callback) = 0;
  virtual void stop(std::string_view track) = 0;
};
} // namespace studio_remote
