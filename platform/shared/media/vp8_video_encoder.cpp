#include "vp8_video_encoder.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace studio_remote
{
Vp8VideoEncoder::Vp8VideoEncoder(Vp8EncoderBindings bindings) : bindings_(std::move(bindings))
{
  if (!bindings_.allocate_image || !bindings_.destroy_codec)
    throw std::invalid_argument("VP8 resource bindings are incomplete");
}

Vp8VideoEncoder::~Vp8VideoEncoder()
{
  if (initialized)
    bindings_.destroy_codec(&codec);
  if (image.img_data)
    vpx_img_free(&image);
}

void Vp8VideoEncoder::configure(std::uint32_t w,
                                std::uint32_t h,
                                std::uint32_t fps,
                                std::uint32_t bitrate)
{
  if (initialized && image.img_data && width == w && height == h && frames_per_second == fps &&
      bitrate_kbps == bitrate)
    return;
  if (initialized)
  {
    bindings_.destroy_codec(&codec);
    initialized = false;
  }
  if (image.img_data)
  {
    vpx_img_free(&image);
    image = {};
  }
  width = w;
  height = h;
  frames_per_second = fps;
  bitrate_kbps = bitrate;
  vpx_codec_enc_cfg_t cfg{};
  if (vpx_codec_enc_config_default(vpx_codec_vp8_cx(), &cfg, 0) != VPX_CODEC_OK)
    throw std::runtime_error("VP8 configuration failed");
  cfg.g_w = w;
  cfg.g_h = h;
  cfg.g_timebase = {1, 90000};
  cfg.g_lag_in_frames = 0;
  cfg.rc_end_usage = VPX_CBR;
  cfg.rc_target_bitrate = bitrate_kbps;
  cfg.rc_min_quantizer = 4;
  cfg.rc_max_quantizer = 48;
  cfg.rc_buf_initial_sz = 500;
  cfg.rc_buf_optimal_sz = 600;
  cfg.rc_buf_sz = 1000;
  cfg.g_threads = 4;
  cfg.g_error_resilient = VPX_ERROR_RESILIENT_DEFAULT;
  cfg.kf_max_dist = frames_per_second * 2;
  if (vpx_codec_enc_init(&codec, vpx_codec_vp8_cx(), &cfg, 0) != VPX_CODEC_OK)
    throw std::runtime_error("VP8 encoder failed");
  // Codec ownership begins before any subsequent native operation can fail.
  initialized = true;
  vpx_codec_control(&codec, VP8E_SET_CPUUSED, 5);
  vpx_codec_control(&codec, VP8E_SET_STATIC_THRESHOLD, 1);
  if (!bindings_.allocate_image(&image, VPX_IMG_FMT_I420, w, h, 1))
    throw std::runtime_error("I420 allocation failed");
}

void Vp8VideoEncoder::convert(const RawVideoFrame& f)
{
  // Bilinear quality is unnecessary at the 15 fps monitoring resolution; center-sampled scaling
  // keeps latency bounded.
  auto byte = [](int value) { return static_cast<uint8_t>(std::clamp(value, 0, 255)); };
  for (uint32_t y = 0; y < height; ++y)
  {
    const uint32_t sy = height == f.height ? y : y * f.height / height;
    for (uint32_t x = 0; x < width; ++x)
    {
      const uint32_t sx = width == f.width ? x : x * f.width / width;
      const uint8_t* p = &f.bgra[sy * f.stride + sx * 4];
      int b = p[0], g = p[1], r = p[2];
      image.planes[0][y * image.stride[0] + x] =
          byte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
    }
  }
  for (uint32_t y = 0; y < height / 2; ++y)
  {
    const uint32_t sy = height == f.height ? y * 2 : (y * 2) * f.height / height;
    for (uint32_t x = 0; x < width / 2; ++x)
    {
      const uint32_t sx = width == f.width ? x * 2 : (x * 2) * f.width / width;
      const uint8_t* p = &f.bgra[sy * f.stride + sx * 4];
      int b = p[0], g = p[1], r = p[2];
      image.planes[1][y * image.stride[1] + x] =
          byte(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
      image.planes[2][y * image.stride[2] + x] =
          byte(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
    }
  }
}
} // namespace studio_remote
