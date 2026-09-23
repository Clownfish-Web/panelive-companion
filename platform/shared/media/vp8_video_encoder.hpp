#pragma once

#include <studio_remote/media/frame_cadence.hpp>
#include <studio_remote/media/media.hpp>

#include <vpx/vp8cx.h>
#include <vpx/vpx_encoder.h>

#include <cstdint>
#include <functional>

namespace studio_remote
{
class WebRtcHub;

// Private native-library bindings. Allocated images must remain compatible with vpx_img_free;
// codec destruction must not throw. Normal operation uses libvpx directly.
struct Vp8EncoderBindings
{
  std::function<vpx_image_t*(vpx_image_t*, vpx_img_fmt_t, unsigned, unsigned, unsigned)>
      allocate_image = vpx_img_alloc;
  std::function<vpx_codec_err_t(vpx_codec_ctx_t*)> destroy_codec = vpx_codec_destroy;
};

class Vp8VideoEncoder
{
public:
  explicit Vp8VideoEncoder(Vp8EncoderBindings bindings = {});
  ~Vp8VideoEncoder();
  Vp8VideoEncoder(const Vp8VideoEncoder&) = delete;
  Vp8VideoEncoder& operator=(const Vp8VideoEncoder&) = delete;
  void configure(std::uint32_t width,
                 std::uint32_t height,
                 std::uint32_t frames_per_second,
                 std::uint32_t bitrate_kbps);

private:
  // The existing transport owner borrows the native context/image during synchronous encoding;
  // no other caller can extend their lifetime or mutate native resource state.
  friend class WebRtcHub;
  void convert(const RawVideoFrame& frame);
  Vp8EncoderBindings bindings_;
  vpx_codec_ctx_t codec{};
  vpx_image_t image{};
  bool initialized = false;
  std::uint32_t width = 0, height = 0;
  std::uint32_t frames_per_second = 0, bitrate_kbps = 0;
  FrameCadence cadence;
};
} // namespace studio_remote
