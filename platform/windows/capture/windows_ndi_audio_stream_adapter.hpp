#pragma once

#include "windows_ndi_runtime.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <studio_remote/vmix/vmix_audio_stream_adapter.hpp>
#include <thread>
#include <vector>

namespace studio_remote::windows
{

class NdiAudioStreamAdapter final : public vmix::IVmixAudioStreamAdapter
{
public:
  NdiAudioStreamAdapter(std::shared_ptr<NdiRuntime> runtime,
                        NdiSourceDescriptor monitor_source,
                        NdiSourceDescriptor master_source,
                        vmix::AudioStreamAdapterCallbacks callbacks);
  ~NdiAudioStreamAdapter();

  NdiAudioStreamAdapter(const NdiAudioStreamAdapter&) = delete;
  NdiAudioStreamAdapter& operator=(const NdiAudioStreamAdapter&) = delete;

  void start() override;
  void stop() override;
  vmix::AudioStreamAvailability availability() const override;

private:
  struct TrackState
  {
    std::string name;
    NdiSourceDescriptor source;
    std::thread worker;
    std::vector<float> samples;
    std::uint64_t sequence = 0;
    std::uint64_t sample_index = 0;
    std::uint64_t last_frame_ns = 0;
    std::uint64_t dropped_frames = 0;
    std::uint64_t callback_count = 0;
    std::string error;
  };

  void receive_loop(TrackState& track);
  void consume_audio(TrackState& track, const void* audio_frame);
  void publish_status(const TrackState& track) const;
  AudioMonitorStatus status(const TrackState& track) const;

  std::shared_ptr<NdiRuntime> runtime_;
  vmix::AudioStreamAdapterCallbacks callbacks_;
  mutable std::mutex mutex_;
  TrackState monitor_{"monitor"};
  TrackState master_{"master"};
  std::atomic<bool> stopping_{false};
  bool started_ = false;
};

} // namespace studio_remote::windows
