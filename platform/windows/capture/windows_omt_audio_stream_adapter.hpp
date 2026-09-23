#pragma once

#include "windows_omt_runtime.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <studio_remote/vmix/vmix_audio_stream_adapter.hpp>
#include <thread>
#include <vector>

namespace studio_remote::windows
{

class OmtAudioStreamAdapter final : public vmix::IVmixAudioStreamAdapter
{
public:
  OmtAudioStreamAdapter(std::shared_ptr<IOmtRuntime> runtime,
                        OmtSourceDescriptor monitor_source,
                        OmtSourceDescriptor master_source,
                        vmix::AudioStreamAdapterCallbacks callbacks);
  ~OmtAudioStreamAdapter();

  OmtAudioStreamAdapter(const OmtAudioStreamAdapter&) = delete;
  OmtAudioStreamAdapter& operator=(const OmtAudioStreamAdapter&) = delete;

  void start() override;
  void stop() override;
  vmix::AudioStreamAvailability availability() const override;

private:
  struct TrackState
  {
    std::string name;
    OmtSourceDescriptor source;
    std::thread worker;
    std::vector<float> samples;
    std::uint64_t sequence = 0;
    std::uint64_t sample_index = 0;
    std::uint64_t last_frame_ns = 0;
    std::uint64_t dropped_frames = 0;
    std::uint64_t callback_count = 0;
    std::string error;
  };

  void receive_loop(TrackState& track) noexcept;
  void consume_audio(TrackState& track, OmtAudioFrame frame);
  AudioMonitorStatus status(const TrackState& track) const;
  void publish_status(const TrackState& track) const;

  std::shared_ptr<IOmtRuntime> runtime_;
  vmix::AudioStreamAdapterCallbacks callbacks_;
  TrackState monitor_{"monitor"};
  TrackState master_{"master"};
  mutable std::mutex mutex_;
  std::atomic<bool> stopping_{false};
  bool started_ = false;
};

} // namespace studio_remote::windows
