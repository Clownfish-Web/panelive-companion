#pragma once

#include <memory>
#include <string>
#include <studio_remote/media/media.hpp>

namespace studio_remote::windows
{
class SrtSequentialStream;

class WindowsMpegTsVideoDecoder
{
public:
  explicit WindowsMpegTsVideoDecoder(SrtSequentialStream* transport_stream);
  ~WindowsMpegTsVideoDecoder();

  WindowsMpegTsVideoDecoder(const WindowsMpegTsVideoDecoder&) = delete;
  WindowsMpegTsVideoDecoder& operator=(const WindowsMpegTsVideoDecoder&) = delete;

  /// Blocks until a decoded frame is available. Returns empty only for an orderly end of stream.
  std::shared_ptr<RawVideoFrame> read_frame();

private:
  struct Impl;
  std::unique_ptr<Impl> implementation_;
};

} // namespace studio_remote::windows
