#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace studio_remote
{

enum class ThumbnailTarget
{
  source,
  scene
};

struct ThumbnailCaptureRequest
{
  ThumbnailTarget target = ThumbnailTarget::source;
  std::string resource_id;
  std::string resource_name;
  std::uint32_t maximum_width = 0;
  std::uint32_t maximum_height = 0;
};

struct RawThumbnail
{
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  std::uint64_t observed_at_ns = 0;
  std::vector<std::uint8_t> bgra;
};

class IThumbnailCapture
{
public:
  virtual ~IThumbnailCapture() = default;
  virtual bool available() const noexcept
  {
    return true;
  }
  virtual RawThumbnail capture(const ThumbnailCaptureRequest& request) = 0;
};

class ThumbnailCaptureError : public std::runtime_error
{
public:
  ThumbnailCaptureError(std::string code, std::string message)
      : std::runtime_error(std::move(message)), code_(std::move(code))
  {
  }

  const std::string& code() const noexcept
  {
    return code_;
  }

private:
  std::string code_;
};

struct EncodedThumbnail
{
  std::string content_type;
  std::vector<std::uint8_t> bytes;
};

class IThumbnailEncoder
{
public:
  virtual ~IThumbnailEncoder() = default;
  virtual EncodedThumbnail encode(const RawThumbnail& image) = 0;
};

} // namespace studio_remote
