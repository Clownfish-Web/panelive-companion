#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/media/audio_monitor.hpp>
#include <studio_remote/media/media.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>

namespace studio_remote
{
class EnduranceEventLog;
class StateStore;
class WebRtcHub;

bool audio_capability_status_changed(const AudioMonitorStatus& previous,
                                     const AudioMonitorStatus& current);

/// Windows OBS media callbacks shared by composition roots. All resources and callback targets
/// are borrowed and outlive this object. Owns no workers or lifecycle controller. Video callbacks
/// have one capture-thread caller; audio callbacks have one reader-thread caller. Install/reset
/// publishers only while those workers are stopped. Publishers run outside the capability mutex.
class ObsMediaDelivery final
{
public:
  ObsMediaDelivery(StateStore& state,
                   WebRtcHub& webrtc,
                   std::function<void(const AudioMonitorStatus&)> diagnostic_status,
                   EnduranceEventLog* log);

  void on_capabilities_changed(std::function<void()> publisher);
  void on_capture_generation_changed(std::function<void(std::uint32_t)> publisher);
  void receive_video(std::string_view track, std::shared_ptr<const RawVideoFrame> frame);
  void receive_video_health(std::string_view track, const json& health);
  void receive_capture_generation(std::uint32_t generation);
  void receive_audio_status(const AudioMonitorStatus& status);

private:
  StateStore& state_;
  WebRtcHub& webrtc_;
  const std::function<void(const AudioMonitorStatus&)> diagnostic_status_;
  EnduranceEventLog* const log_;
  std::atomic<std::uint64_t> program_health_second_{0}, preview_health_second_{0};
  std::atomic<std::uint64_t> program_epoch_{1}, preview_epoch_{1};
  std::function<void(std::uint32_t)> reset_media_sessions_;
  std::mutex audio_capability_mutex_;
  std::optional<AudioMonitorStatus> previous_audio_status_;
  std::function<void()> publish_capabilities_;
};
} // namespace studio_remote
