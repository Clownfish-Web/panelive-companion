#include "obs_bgra_converter.hpp"

#include <studio_remote/ipc/video_frame_mapping.hpp>
#include <util/base.h>

#include <new>

namespace studio_remote::obs_plugin
{

void ObsBgraConverter::ScalerDeleter::operator()(video_scaler_t* scaler) const noexcept
{
  video_scaler_destroy(scaler);
}

bool ObsBgraConverter::configure(const video_scale_info& source,
                                 std::uint32_t width,
                                 std::uint32_t height)
{
  reset();
  if (width == 0 || height == 0 || width > ipc::maximum_frame_width ||
      height > ipc::maximum_frame_height)
    return false;

  video_scale_info destination{};
  destination.format = VIDEO_FORMAT_BGRA;
  destination.width = width;
  destination.height = height;
  destination.range = VIDEO_RANGE_FULL;
  destination.colorspace = VIDEO_CS_709;
  video_scaler_t* scaler = nullptr;
  const auto result =
      video_scaler_create(&scaler, &destination, &source, VIDEO_SCALE_FAST_BILINEAR);
  scaler_.reset(scaler);
  if (result != VIDEO_SCALER_SUCCESS)
  {
    reset();
    return false;
  }
  try
  {
    pixels_.resize(static_cast<std::size_t>(width) * height * ipc::bytes_per_pixel);
  }
  catch (const std::bad_alloc&)
  {
    blog(LOG_ERROR, "[PaneLive Companion] Could not allocate Program conversion pixels");
    reset();
    return false;
  }
  converted_.data[0] = pixels_.data();
  converted_.linesize[0] = width * ipc::bytes_per_pixel;
  return true;
}

const video_data* ObsBgraConverter::convert(const video_data& frame, std::uint32_t requested_fps)
{
  if (!scaler_ || requested_fps == 0 || !cadence_.should_publish(frame.timestamp, requested_fps))
    return nullptr;

  if (!video_scaler_scale(
          scaler_.get(), converted_.data, converted_.linesize, frame.data, frame.linesize))
  {
    if (!failure_logged_)
      blog(LOG_ERROR, "[PaneLive Companion] Program frame conversion failed");
    failure_logged_ = true;
    return nullptr;
  }
  failure_logged_ = false;
  converted_.timestamp = frame.timestamp;
  return &converted_;
}

void ObsBgraConverter::reset()
{
  scaler_.reset();
  pixels_.clear();
  converted_ = {};
  cadence_ = {};
  failure_logged_ = false;
}

} // namespace studio_remote::obs_plugin
