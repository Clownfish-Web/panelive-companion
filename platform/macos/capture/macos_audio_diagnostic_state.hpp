#pragma once

#include "macos_audio_diagnostic_player.hpp"
#include "macos_audio_output_endpoint.hpp"

#include <studio_remote/media/audio_diagnostic_peak_window.hpp>
#include <studio_remote/media/audio_diagnostic_session_log.hpp>
#include <studio_remote/media/audio_playout_controller.hpp>

#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace studio_remote::macos
{
// Private implementation shared by the control facade and playback translation units. The
// state mutex owns all presentation, queue, gain, and policy values; native calls happen outside
// it.
struct MacosAudioDiagnosticPlayer::Impl
{
  explicit Impl(RealtimeAudioServices* services) : realtime_audio(services)
  {
  }
  struct Block
  {
    std::array<float, 1920> samples{};
    std::uint64_t written = 0;
    std::uint64_t clipped = 0;
    bool playout_active = false;
  };

  void stop_worker();
  bool expire_locked(std::uint64_t now);
  void record_frame_locked(const AudioMonitorFrame& frame,
                           AudioDiagnosticTap tap,
                           float peak,
                           std::uint64_t now);
  void run(std::stop_token stop, const std::string& device);
  void run_once(std::stop_token stop, const std::string& device);
  Block render_block();
  void commit_block(const Block& block);
  void observe_clock(MacosAudioOutputEndpoint& endpoint);
  bool expired();

  mutable std::mutex mutex;
  std::mutex lifecycle_mutex;
  std::mutex wait_mutex;
  std::condition_variable_any wake;
  std::deque<SharedAudioMonitorFrame> frames;
  AudioDiagnosticPlaybackStatus status;
  float gain_db = 0;
  std::uint64_t next_sample_index = 0;
  std::uint32_t topology_generation = 0;
  std::uint64_t queue_generation = 0;
  AudioPlayoutController playout;
  AudioDiagnosticPeakWindow peak_window;
  AudioDiagnosticSessionLog diagnostic_log;
  AudioMonitorStatus monitor_status;
  RealtimeAudioServices* realtime_audio;
  std::jthread thread;
};
} // namespace studio_remote::macos
