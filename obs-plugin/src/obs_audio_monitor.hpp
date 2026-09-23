#pragma once

#include "frontend_reload_barrier.hpp"
#include "obs_audio_mix_policy.hpp"
#include "obs_audio_source_ingress.hpp"

#include <studio_remote/ipc/audio_monitor_mapping.hpp>

#ifdef _WIN32
#include "windows/shared_memory_owner.hpp"
#elif defined(__APPLE__)
#include "macos/shared_memory_owner.hpp"
#endif
#include <obs-frontend-api.h>
#include <obs.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace studio_remote::obs_plugin
{

class ObsAudioMonitor
{
public:
  ObsAudioMonitor();
  ~ObsAudioMonitor();

  ObsAudioMonitor(const ObsAudioMonitor&) = delete;
  ObsAudioMonitor& operator=(const ObsAudioMonitor&) = delete;

  bool start();
  void stop();

private:
  struct MixFrame
  {
    std::array<double, ipc::audio_monitor_frame_samples * ipc::audio_monitor_maximum_channels>
        samples{};
    std::array<std::uint16_t, ipc::audio_monitor_frame_samples> coverage{};
    std::size_t active_source_count = 0;
    bool overlap_repaired = false;
    bool discontinuity = false;
  };

  static void on_main_render(void* context, std::uint32_t width, std::uint32_t height);
  static void handle_frontend_event(obs_frontend_event event, void* context);
  void refresh_sources(std::uint64_t now_ns);
  void release_sources_for_reload();
  void detach_sources();
  void mixer_loop();
  void consume_source(ObsAudioSourceIngress& source);
  void mix_block(ObsAudioSourceIngress& source, const ObsAudioIngressBlock& block);
  void grow_publication_delay(std::uint64_t late_samples);
  void publish_due(std::uint64_t now_ns);
  void publish(std::uint64_t first_sample_index, MixFrame frame, std::size_t source_count);
  bool open_mapping();
  void close_mapping();

#ifdef _WIN32
  std::unique_ptr<windows::SharedMemoryOwner> mapping_owner_;
#elif defined(__APPLE__)
  std::unique_ptr<macos::SharedMemoryOwner> mapping_owner_;
#else
  int mapping_handle_ = -1;
#endif
  ipc::AudioMonitorMapping* mapping_ = nullptr;
#ifdef _WIN32
  std::unique_ptr<windows::SharedMemoryOwner> control_owner_;
#elif defined(__APPLE__)
  std::unique_ptr<macos::SharedMemoryOwner> control_owner_;
#else
  int control_handle_ = -1;
#endif
  ipc::AudioMonitorControlMapping* control_ = nullptr;
  std::unordered_map<std::string, std::shared_ptr<ObsAudioSourceIngress>> taps_;
  std::mutex taps_mutex_;
  std::map<std::uint64_t, MixFrame> mix_frames_;
  std::thread mixer_thread_;
  std::atomic<bool> stopping_{false};
  FrontendReloadBarrier reload_barrier_;
  ObsAudioIngressCounters ingress_counters_;
  std::uint64_t last_refresh_ns_ = 0;
  std::uint64_t sequence_ = 0;
  std::uint64_t next_publish_sample_ = 0;
  std::uint64_t publication_delay_ns_ = audio_initial_publication_delay_ns;
  std::atomic<std::uint32_t> topology_generation_{0};
  std::uint32_t input_channels_ = 0;
  float limiter_gain_ = 1.0F;
  AudioMonitorHeadroomState headroom_state_;
  bool started_ = false;
  bool frontend_callback_registered_ = false;
};

} // namespace studio_remote::obs_plugin
