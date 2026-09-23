#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <studio_remote/capture/capture_provider.hpp>
#include <vector>

namespace studio_remote
{
struct RawVideoFrame
{
  std::uint32_t width = 0, height = 0, stride = 0;
  std::uint64_t captured_ns = 0;
  std::vector<std::uint8_t> bgra;
};
struct EncodedFrame
{
  std::string codec = "VP8";
  std::uint32_t timestamp = 0;
  bool keyframe = false;
  std::vector<std::byte> bytes;
};
class IFrameConverter
{
public:
  virtual ~IFrameConverter() = default;
  virtual RawVideoFrame convert(const MediaFrame&, std::uint32_t, std::uint32_t) = 0;
};
class IVideoEncoder
{
public:
  virtual ~IVideoEncoder() = default;
  virtual std::vector<EncodedFrame> encode(const RawVideoFrame&) = 0;
  virtual void request_keyframe() = 0;
};
} // namespace studio_remote
