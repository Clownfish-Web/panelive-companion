#pragma once

#include "srt_sequential_stream.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <studio_remote/vmix/vmix_video_stream_adapter.hpp>
#include <thread>

namespace studio_remote::windows
{

struct VmixSrtFeedEndpoint
{
  std::string host;
  std::uint16_t port = 0;
  std::uint8_t output = 0;
  std::uint32_t latency_ms = 160;
};

class VmixSrtVideoStreamAdapter final : public vmix::IVmixVideoStreamAdapter
{
public:
  VmixSrtVideoStreamAdapter(VmixSrtFeedEndpoint program,
                            VmixSrtFeedEndpoint preview,
                            vmix::VideoStreamAdapterCallbacks callbacks);
  ~VmixSrtVideoStreamAdapter() override;

  VmixSrtVideoStreamAdapter(const VmixSrtVideoStreamAdapter&) = delete;
  VmixSrtVideoStreamAdapter& operator=(const VmixSrtVideoStreamAdapter&) = delete;

  void start() override;
  void stop() override;
  vmix::VideoStreamAvailability availability() const override;

private:
  struct TrackState
  {
    std::string name;
    VmixSrtFeedEndpoint endpoint;
    std::thread worker;
    mutable std::mutex stream_mutex;
    SrtSequentialStream* active_stream = nullptr;
    std::atomic<std::uint64_t> last_frame_ns{0};
    std::atomic<std::uint64_t> received_frames{0};
    std::atomic<std::uint64_t> reconnects{0};
    std::atomic<std::uint32_t> width{0};
    std::atomic<std::uint32_t> height{0};
  };

  void run_track(TrackState& track);
  void publish_health(const TrackState& track, bool available, std::string_view error = {}) const;
  bool wait_before_retry(std::size_t retry_index);
  static bool track_available(const TrackState& track) noexcept;

  TrackState program_;
  TrackState preview_;
  vmix::VideoStreamAdapterCallbacks callbacks_;
  mutable std::mutex lifecycle_mutex_;
  std::condition_variable wake_;
  std::atomic<bool> stopping_{false};
  bool running_ = false;
  bool srt_started_ = false;
};

} // namespace studio_remote::windows
