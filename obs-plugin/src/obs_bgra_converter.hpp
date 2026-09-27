#pragma once

#include <media-io/video-io.h>
#include <media-io/video-scaler.h>
#include <studio_remote/media/frame_cadence.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace studio_remote::obs_plugin
{

// Configure/reset only while the raw callback is disconnected. Conversion and cadence belong
// to that callback's video thread. Returned pixels are borrowed until the next conversion.
class ObsBgraConverter
{
public:
  bool configure(const video_scale_info& source, std::uint32_t width, std::uint32_t height);
  const video_data* convert(const video_data& frame, std::uint32_t requested_fps);
  void reset();

private:
  struct ScalerDeleter
  {
    void operator()(video_scaler_t* scaler) const noexcept;
  };

  std::unique_ptr<video_scaler_t, ScalerDeleter> scaler_;
  std::vector<std::uint8_t> pixels_;
  video_data converted_{};
  FrameCadence cadence_;
  bool failure_logged_ = false;
};

} // namespace studio_remote::obs_plugin
