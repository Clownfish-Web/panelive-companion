#pragma once

#include "shared_thumbnail_mailbox.hpp"
#include "thumbnail_capture_policy.hpp"

#include <obs.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace studio_remote::obs_plugin
{

class ObsSourceThumbnailRenderer
{
public:
  explicit ObsSourceThumbnailRenderer(SharedThumbnailMailbox& mailbox);
  ~ObsSourceThumbnailRenderer();

  bool start();
  void stop();

private:
  struct CaptureAttempt
  {
    bool copied = false;
    bool black = false;
    bool source_missing = false;
    bool source_unavailable = false;
    bool activation_required = false;
    double average_channel = 0.0;
    std::size_t non_black_pixels = 0;
    std::size_t total_pixels = 0;
  };

  static void render_callback(void* context, std::uint32_t width, std::uint32_t height);
  void render();
  void begin_request(ThumbnailRequest request);
  void finish_request(ipc::ThumbnailStatus status);
  CaptureAttempt capture_source();
  bool warmup_expired(std::uint64_t now_ns) const;
  // Thumbnail rendering may make a source showing, as OBS's own screenshot path does, but must
  // never add a MAIN_VIEW active reference. Active references can start media and monitored audio.
  void log_reference_state(obs_source_t* source,
                           const char* phase,
                           std::uint32_t owned_active_references,
                           std::uint32_t owned_showing_references) const;

  SharedThumbnailMailbox& mailbox_;
  bool started_ = false;
  std::uint64_t last_request_sequence_ = 0;
  std::optional<ThumbnailRequest> request_;
  obs_weak_source_t* source_ = nullptr;
  std::string source_kind_;
  ThumbnailCapturePolicy capture_policy_ = ThumbnailCapturePolicy::showing_only_render;
  bool monitor_capture_ = false;
  std::uint32_t render_cycles_ = 0;
  std::uint32_t next_capture_cycle_ = 0;
  std::uint32_t capture_attempts_ = 0;
  std::uint64_t activation_started_ns_ = 0;
};

} // namespace studio_remote::obs_plugin
