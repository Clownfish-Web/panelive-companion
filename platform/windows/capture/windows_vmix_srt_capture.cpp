#include "windows_vmix_srt_capture.hpp"

#include "windows_mpeg_ts_video_decoder.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <random>
#include <srt/srt.h>
#include <stdexcept>
#include <studio_remote/media/frame_cadence.hpp>

namespace studio_remote::windows
{
namespace
{
constexpr std::uint64_t stale_frame_ns = 2'000'000'000ULL;
constexpr std::array<std::chrono::milliseconds, 6> reconnect_delays{
    std::chrono::milliseconds(250),
    std::chrono::milliseconds(500),
    std::chrono::milliseconds(1'000),
    std::chrono::milliseconds(2'000),
    std::chrono::milliseconds(3'000),
    std::chrono::milliseconds(5'000)};

std::uint64_t monotonic_nanoseconds() noexcept
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

void validate_endpoint(const VmixSrtFeedEndpoint& endpoint)
{
  if (endpoint.host.empty() || endpoint.port == 0 || endpoint.output < 1 || endpoint.output > 4 ||
      endpoint.latency_ms < 20 || endpoint.latency_ms > 8'000)
    throw std::invalid_argument("Invalid vMix SRT feed endpoint");
}
} // namespace

VmixSrtVideoStreamAdapter::VmixSrtVideoStreamAdapter(VmixSrtFeedEndpoint program,
                                                     VmixSrtFeedEndpoint preview,
                                                     vmix::VideoStreamAdapterCallbacks callbacks)
    : program_{"program", std::move(program)}, preview_{"preview", std::move(preview)},
      callbacks_(std::move(callbacks))
{
  validate_endpoint(program_.endpoint);
  validate_endpoint(preview_.endpoint);
  if (program_.endpoint.output == preview_.endpoint.output ||
      program_.endpoint.port == preview_.endpoint.port)
    throw std::invalid_argument("vMix SRT Program and Preview endpoints must be distinct");
  if (!callbacks_.frame || !callbacks_.health || !callbacks_.target_frame_rate)
    throw std::invalid_argument("Complete vMix video stream callbacks are required");
}

VmixSrtVideoStreamAdapter::~VmixSrtVideoStreamAdapter()
{
  stop();
}

void VmixSrtVideoStreamAdapter::start()
{
  std::lock_guard lock(lifecycle_mutex_);
  if (running_)
    return;
  if (srt_startup() != 0)
    throw std::runtime_error("SRT runtime startup failed");
  srt_started_ = true;
  stopping_ = false;
  running_ = true;
  publish_health(program_, false, "media-negotiating");
  publish_health(preview_, false, "media-negotiating");
  program_.worker = std::thread([this] { run_track(program_); });
  preview_.worker = std::thread([this] { run_track(preview_); });
}

void VmixSrtVideoStreamAdapter::stop()
{
  {
    std::lock_guard lock(lifecycle_mutex_);
    if (!running_)
      return;
    stopping_ = true;
  }
  for (auto* track : {&program_, &preview_})
  {
    std::lock_guard stream_lock(track->stream_mutex);
    if (track->active_stream)
      track->active_stream->cancel();
  }
  wake_.notify_all();
  if (program_.worker.joinable())
    program_.worker.join();
  if (preview_.worker.joinable())
    preview_.worker.join();

  std::lock_guard lock(lifecycle_mutex_);
  if (srt_started_)
  {
    srt_cleanup();
    srt_started_ = false;
  }
  running_ = false;
}

void VmixSrtVideoStreamAdapter::run_track(TrackState& track)
{
  std::size_t retry_index = 0;
  while (!stopping_)
  {
    std::string error;
    auto stream = SrtSequentialStream::connect(
        track.endpoint.host, track.endpoint.port, track.endpoint.latency_ms, error);
    if (!stream)
    {
      ++track.reconnects;
      publish_health(track, false, error);
      if (wait_before_retry(retry_index++))
        break;
      continue;
    }

    {
      std::lock_guard stream_lock(track.stream_mutex);
      track.active_stream = stream.get();
    }
    try
    {
      WindowsMpegTsVideoDecoder decoder(stream.get());
      FrameCadence cadence;
      retry_index = 0;
      while (!stopping_)
      {
        auto frame = decoder.read_frame();
        if (!frame)
          throw std::runtime_error("vMix SRT stream ended");
        track.last_frame_ns = frame->captured_ns;
        ++track.received_frames;
        track.width = frame->width;
        track.height = frame->height;
        if (cadence.should_publish(frame->captured_ns, callbacks_.target_frame_rate(track.name)))
          callbacks_.frame(track.name, std::move(frame));
        publish_health(track, true);
      }
    }
    catch (const std::exception& exception)
    {
      if (!stopping_)
      {
        ++track.reconnects;
        publish_health(track, false, exception.what());
      }
    }
    {
      std::lock_guard stream_lock(track.stream_mutex);
      track.active_stream = nullptr;
    }
    stream->cancel();
    if (!stopping_ && wait_before_retry(retry_index++))
      break;
  }
  publish_health(track, false, "stopped");
}

bool VmixSrtVideoStreamAdapter::wait_before_retry(std::size_t retry_index)
{
  const auto base = reconnect_delays[std::min(retry_index, reconnect_delays.size() - 1)];
  thread_local std::mt19937 random(std::random_device{}());
  std::uniform_int_distribution<int> jitter(-20, 20);
  const auto delay = base + base * jitter(random) / 100;
  std::unique_lock lock(lifecycle_mutex_);
  return wake_.wait_for(lock, delay, [this] { return stopping_.load(); });
}

void VmixSrtVideoStreamAdapter::publish_health(const TrackState& track,
                                               bool available,
                                               std::string_view error) const
{
  const auto last_frame = track.last_frame_ns.load();
  const auto now = monotonic_nanoseconds();
  callbacks_.health(
      track.name,
      {{"available", available},
       {"backend", "vmix-srt"},
       {"output", track.endpoint.output},
       {"port", track.endpoint.port},
       {"latencyMs", track.endpoint.latency_ms},
       {"receivedFrames", track.received_frames.load()},
       {"reconnects", track.reconnects.load()},
       {"width", track.width.load()},
       {"height", track.height.load()},
       {"effectiveFps", callbacks_.target_frame_rate(track.name)},
       {"ageMs", last_frame == 0 || now < last_frame ? 0 : (now - last_frame) / 1'000'000ULL},
       {"error", std::string(error)}});
}

bool VmixSrtVideoStreamAdapter::track_available(const TrackState& track) noexcept
{
  const auto last_frame = track.last_frame_ns.load();
  const auto now = monotonic_nanoseconds();
  return last_frame != 0 && now >= last_frame && now - last_frame <= stale_frame_ns;
}

vmix::VideoStreamAvailability VmixSrtVideoStreamAdapter::availability() const
{
  return {track_available(program_), track_available(preview_)};
}

} // namespace studio_remote::windows
