#include "windows_ndi_capture.hpp"

#include "ndi_video_frame_normalizer.hpp"

#include <studio_remote/media/frame_cadence.hpp>

#include <Processing.NDI.Lib.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace studio_remote::windows
{
namespace
{

constexpr std::uint64_t unavailable_after_ns = 2'000'000'000ULL;

std::uint64_t now_ns()
{
  return monotonic_ns();
}

} // namespace

NdiCaptureManager::NdiCaptureManager(std::shared_ptr<NdiRuntime> runtime,
                                     NdiSourceDescriptor program_source,
                                     NdiSourceDescriptor preview_source,
                                     vmix::VideoStreamAdapterCallbacks video_callbacks,
                                     CanvasAspectRatioProvider canvas_aspect_ratio_provider)
    : runtime_(std::move(runtime)), video_callbacks_(std::move(video_callbacks)),
      canvas_aspect_ratio_provider_(std::move(canvas_aspect_ratio_provider))
{
  if (!runtime_)
    throw std::invalid_argument("The NDI runtime is required");
  if (!video_callbacks_.frame)
    throw std::invalid_argument("The vMix video frame callback is required");
  program_.source = std::move(program_source);
  preview_.source = std::move(preview_source);
}

NdiCaptureManager::~NdiCaptureManager()
{
  stop();
}

void NdiCaptureManager::start()
{
  if (initialized_)
    return;
  if (program_.source.name.empty() || preview_.source.name.empty())
    throw std::invalid_argument("Both vMix Program and Preview NDI source names are required");
  initialized_ = true;
  stopping_.store(false);
  program_.worker = std::thread([this] { receive_loop(program_); });
  preview_.worker = std::thread([this] { receive_loop(preview_); });
}

void NdiCaptureManager::stop()
{
  stopping_.store(true);
  if (program_.worker.joinable())
    program_.worker.join();
  if (preview_.worker.joinable())
    preview_.worker.join();
  initialized_ = false;
}

void NdiCaptureManager::receive_loop(TrackState& track)
{
  auto receiver = runtime_->create_video_receiver(track.source);
  if (!receiver)
  {
    {
      std::lock_guard lock(mutex_);
      track.error = "Could not create the NDI receiver";
    }
    publish_health(track, false);
    return;
  }

  FrameCadence cadence;
  bool previously_available = false;
  while (!stopping_.load())
  {
    NDIlib_video_frame_v2_t video{};
    if (!runtime_->capture_video(receiver, &video, 200))
    {
      bool available = false;
      {
        std::lock_guard lock(mutex_);
        available =
            track.last_frame_ns != 0 && now_ns() - track.last_frame_ns < unavailable_after_ns;
      }
      if (available != previously_available)
      {
        publish_health(track, available);
        previously_available = available;
      }
      continue;
    }

    const auto observed_ns = now_ns();
    const auto requested_fps = std::clamp(
        video_callbacks_.target_frame_rate ? video_callbacks_.target_frame_rate(track.name) : 15U,
        1U,
        15U);
    if (!cadence.should_publish(observed_ns, requested_fps))
    {
      {
        std::lock_guard lock(mutex_);
        ++track.dropped_frames;
      }
      runtime_->free_video(receiver, &video);
      continue;
    }

    const auto source_width = video.xres > 0 ? static_cast<std::uint32_t>(video.xres) : 0;
    const auto source_height = video.yres > 0 ? static_cast<std::uint32_t>(video.yres) : 0;
    const auto picture_aspect_ratio = video.picture_aspect_ratio;
    const auto canvas_aspect_ratio =
        canvas_aspect_ratio_provider_ ? canvas_aspect_ratio_provider_() : 0.0;
    const auto normalized = normalize_ndi_bgra_frame(
        {source_width,
         source_height,
         video.line_stride_in_bytes > 0 ? static_cast<std::uint32_t>(video.line_stride_in_bytes)
                                        : 0,
         picture_aspect_ratio,
         observed_ns,
         video.p_data,
         canvas_aspect_ratio});
    if (!normalized)
    {
      {
        std::lock_guard lock(mutex_);
        ++track.dropped_frames;
        track.error = "unsupported-video-frame";
      }
      runtime_->free_video(receiver, &video);
      continue;
    }
    auto frame = std::make_shared<RawVideoFrame>(std::move(*normalized));
    runtime_->free_video(receiver, &video);

    {
      std::lock_guard lock(mutex_);
      ++track.received_frames;
      track.last_frame_ns = observed_ns;
      track.source_width = source_width;
      track.source_height = source_height;
      track.delivered_width = frame->width;
      track.delivered_height = frame->height;
      track.picture_aspect_ratio = picture_aspect_ratio;
      track.canvas_aspect_ratio = canvas_aspect_ratio;
      track.error.clear();
    }
    if (!previously_available)
    {
      publish_health(track, true);
      previously_available = true;
    }
    video_callbacks_.frame(track.name, std::move(frame));
  }
  runtime_->destroy_receiver(receiver);
}

void NdiCaptureManager::publish_health(const TrackState& track, bool available) const
{
  if (!video_callbacks_.health)
    return;
  json value;
  {
    std::lock_guard lock(mutex_);
    value = {{"available", available},
             {"backend", "vmix-ndi"},
             {"qualityMode", "proxy"},
             {"source", track.source.name},
             {"capturedFrames", track.received_frames},
             {"drops", track.dropped_frames},
             {"sourceWidth", track.source_width},
             {"sourceHeight", track.source_height},
             {"pictureAspectRatio", track.picture_aspect_ratio},
             {"canvasAspectRatio", track.canvas_aspect_ratio},
             {"deliveredWidth", track.delivered_width},
             {"deliveredHeight", track.delivered_height},
             {"ageMs",
              track.last_frame_ns ? json((now_ns() - track.last_frame_ns) / 1'000'000ULL)
                                  : json(nullptr)},
             {"error", track.error.empty() ? json(nullptr) : json(track.error)}};
  }
  video_callbacks_.health(track.name, value);
}

json NdiCaptureManager::health() const
{
  std::lock_guard lock(mutex_);
  const auto value = [](const TrackState& track)
  {
    const auto now = now_ns();
    return json{
        {"available", track.last_frame_ns != 0 && now - track.last_frame_ns < unavailable_after_ns},
        {"backend", "vmix-ndi"},
        {"qualityMode", "proxy"},
        {"source", track.source.name},
        {"capturedFrames", track.received_frames},
        {"drops", track.dropped_frames},
        {"sourceWidth", track.source_width},
        {"sourceHeight", track.source_height},
        {"pictureAspectRatio", track.picture_aspect_ratio},
        {"canvasAspectRatio", track.canvas_aspect_ratio},
        {"deliveredWidth", track.delivered_width},
        {"deliveredHeight", track.delivered_height},
        {"ageMs",
         track.last_frame_ns ? json((now - track.last_frame_ns) / 1'000'000ULL) : json(nullptr)},
        {"error", track.error.empty() ? json(nullptr) : json(track.error)}};
  };
  return {{"program", value(program_)}, {"preview", value(preview_)}};
}

vmix::VideoStreamAvailability NdiCaptureManager::availability() const
{
  std::lock_guard lock(mutex_);
  const auto current_ns = now_ns();
  const auto available = [current_ns](const TrackState& track)
  { return track.last_frame_ns != 0 && current_ns - track.last_frame_ns < unavailable_after_ns; };
  return {available(program_), available(preview_)};
}

} // namespace studio_remote::windows
