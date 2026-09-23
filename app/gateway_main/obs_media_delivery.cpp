#include "obs_media_delivery.hpp"

#include "../../core/transport/libdatachannel_transport.hpp"

#include <studio_remote/observability/endurance_event_log.hpp>
#include <utility>

namespace studio_remote
{
bool audio_capability_status_changed(const AudioMonitorStatus& previous,
                                     const AudioMonitorStatus& current)
{
  return previous.supported != current.supported || previous.available != current.available ||
         previous.configured != current.configured || previous.reason != current.reason ||
         previous.sample_rate != current.sample_rate || previous.channels != current.channels;
}

ObsMediaDelivery::ObsMediaDelivery(StateStore& state,
                                   WebRtcHub& webrtc,
                                   std::function<void(const AudioMonitorStatus&)> diagnostic_status,
                                   EnduranceEventLog* log)
    : state_(state), webrtc_(webrtc), diagnostic_status_(std::move(diagnostic_status)), log_(log)
{
}

void ObsMediaDelivery::on_capabilities_changed(std::function<void()> publisher)
{
  std::lock_guard lock(audio_capability_mutex_);
  publish_capabilities_ = std::move(publisher);
}

void ObsMediaDelivery::on_capture_generation_changed(std::function<void(std::uint32_t)> publisher)
{
  reset_media_sessions_ = std::move(publisher);
}

void ObsMediaDelivery::receive_video(std::string_view track,
                                     std::shared_ptr<const RawVideoFrame> frame)
{
  if (track == "preview" && !state_.studio_mode())
    return;
  const auto captured = frame->captured_ns;
  const auto dimensions = encoded_video_dimensions(frame->width, frame->height);
  webrtc_.push_frame(track, std::move(frame));
  auto& last = track == "program" ? program_health_second_ : preview_health_second_;
  const auto epoch = track == "program" ? program_epoch_.load() : preview_epoch_.load();
  const auto second = captured / 1'000'000'000ULL;
  if (last.exchange(second) != second)
    state_.set_video_health(track,
                            {{"available", true},
                             {"trackEpoch", epoch},
                             {"ageMs", 0},
                             {"width", dimensions.width},
                             {"height", dimensions.height},
                             {"effectiveFps", webrtc_.capture_target_fps(track)},
                             {"observedFrameNs", captured}});
}

void ObsMediaDelivery::receive_video_health(std::string_view track, const json& health)
{
  auto client_health = health;
  client_health["trackEpoch"] = track == "program" ? program_epoch_.load() : preview_epoch_.load();
  state_.set_video_health(track, client_health);
}

void ObsMediaDelivery::receive_capture_generation(std::uint32_t generation)
{
  if (reset_media_sessions_)
    reset_media_sessions_(generation);
  else
    webrtc_.restart_video_sessions();
  const auto program_epoch = program_epoch_.fetch_add(1) + 1;
  const auto preview_epoch = preview_epoch_.fetch_add(1) + 1;
  program_health_second_.store(0);
  preview_health_second_.store(0);
  state_.set_video_health("program", {{"available", false}, {"trackEpoch", program_epoch}});
  state_.set_video_health("preview", {{"available", false}, {"trackEpoch", preview_epoch}});
}

void ObsMediaDelivery::receive_audio_status(const AudioMonitorStatus& status)
{
  if (diagnostic_status_)
    diagnostic_status_(status);
  webrtc_.set_audio_monitor_status(status);
  std::function<void()> publisher;
  {
    std::lock_guard lock(audio_capability_mutex_);
    if (!previous_audio_status_ || audio_capability_status_changed(*previous_audio_status_, status))
    {
      previous_audio_status_ = status;
      publisher = publish_capabilities_;
    }
  }
  if (publisher)
    publisher();
  if (log_)
    log_->record("audioMonitorIpc",
                 "statusChanged",
                 {{"supported", status.supported},
                  {"configured", status.configured},
                  {"available", status.available},
                  {"reason", status.reason},
                  {"ageMs", status.age_ms},
                  {"producerDroppedFrames", status.dropped_frames},
                  {"ipcSkippedFrames", status.ipc_skipped_frames},
                  {"ipcSampleIndexGaps", status.ipc_sample_index_gaps}});
}
} // namespace studio_remote
