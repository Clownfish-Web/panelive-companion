#include "windows_ndi_audio_stream_adapter.hpp"

#include <studio_remote/common/types.hpp>

#include <Processing.NDI.Lib.h>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace studio_remote::windows
{
namespace
{

constexpr std::uint64_t unavailable_after_ns = 2'000'000'000ULL;
constexpr std::uint32_t monitor_sample_rate = 48'000;
constexpr std::uint32_t monitor_channels = 2;
constexpr std::size_t monitor_frame_samples = 960;
constexpr std::size_t maximum_buffered_audio_frames = 4;

std::uint64_t now_ns()
{
  return monotonic_ns();
}

} // namespace

NdiAudioStreamAdapter::NdiAudioStreamAdapter(std::shared_ptr<NdiRuntime> runtime,
                                             NdiSourceDescriptor monitor_source,
                                             NdiSourceDescriptor master_source,
                                             vmix::AudioStreamAdapterCallbacks callbacks)
    : runtime_(std::move(runtime)), callbacks_(std::move(callbacks))
{
  if (!runtime_)
    throw std::invalid_argument("The NDI runtime is required");
  if (!callbacks_.frame)
    throw std::invalid_argument("The vMix audio frame callback is required");
  monitor_.source = std::move(monitor_source);
  master_.source = std::move(master_source);
}

NdiAudioStreamAdapter::~NdiAudioStreamAdapter()
{
  stop();
}

void NdiAudioStreamAdapter::start()
{
  if (started_)
    return;
  started_ = true;
  stopping_.store(false);
  if (!monitor_.source.name.empty())
    monitor_.worker = std::thread([this] { receive_loop(monitor_); });
  if (!master_.source.name.empty())
    master_.worker = std::thread([this] { receive_loop(master_); });
  publish_status(monitor_);
  publish_status(master_);
}

void NdiAudioStreamAdapter::stop()
{
  stopping_.store(true);
  if (monitor_.worker.joinable())
    monitor_.worker.join();
  if (master_.worker.joinable())
    master_.worker.join();
  started_ = false;
}

void NdiAudioStreamAdapter::receive_loop(TrackState& track)
{
  auto receiver = runtime_->create_audio_receiver(track.source);
  if (!receiver)
  {
    {
      std::lock_guard lock(mutex_);
      track.error = "receiver-unavailable";
    }
    publish_status(track);
    return;
  }

  bool previously_available = false;
  while (!stopping_.load())
  {
    NDIlib_audio_frame_v3_t audio{};
    if (runtime_->capture_audio(receiver, &audio, 200))
    {
      consume_audio(track, &audio);
      runtime_->free_audio(receiver, &audio);
    }
    const bool available = status(track).available;
    if (available != previously_available)
    {
      publish_status(track);
      previously_available = available;
    }
  }
  runtime_->destroy_receiver(receiver);
}

void NdiAudioStreamAdapter::consume_audio(TrackState& track, const void* audio_frame)
{
  const auto& audio = *static_cast<const NDIlib_audio_frame_v3_t*>(audio_frame);
  if (audio.sample_rate != monitor_sample_rate || audio.no_channels < 1 || audio.no_samples < 1 ||
      !audio.p_data || audio.channel_stride_in_bytes < audio.no_samples * sizeof(float))
  {
    bool changed = false;
    {
      std::lock_guard lock(mutex_);
      changed = track.error != "unsupported-format";
      track.error = "unsupported-format";
    }
    if (changed)
      publish_status(track);
    return;
  }

  const auto* bytes = reinterpret_cast<const std::uint8_t*>(audio.p_data);
  const auto* left = reinterpret_cast<const float*>(bytes);
  const auto* right = audio.no_channels > 1
                          ? reinterpret_cast<const float*>(bytes + audio.channel_stride_in_bytes)
                          : left;
  track.samples.reserve(track.samples.size() +
                        static_cast<std::size_t>(audio.no_samples) * monitor_channels);
  for (int index = 0; index < audio.no_samples; ++index)
  {
    track.samples.push_back(std::isfinite(left[index]) ? left[index] : 0.0F);
    track.samples.push_back(std::isfinite(right[index]) ? right[index] : 0.0F);
  }

  const auto samples_per_frame = monitor_frame_samples * monitor_channels;
  const auto maximum_samples = samples_per_frame * maximum_buffered_audio_frames;
  if (track.samples.size() > maximum_samples)
  {
    const auto discard = track.samples.size() - maximum_samples;
    track.samples.erase(track.samples.begin(), track.samples.begin() + discard);
    std::lock_guard lock(mutex_);
    ++track.dropped_frames;
  }

  bool became_available = false;
  while (track.samples.size() >= samples_per_frame)
  {
    auto frame = std::make_shared<AudioMonitorFrame>();
    frame->sample_rate = monitor_sample_rate;
    frame->channels = monitor_channels;
    frame->captured_ns = now_ns();
    frame->topology_generation = 1;
    frame->samples.assign(track.samples.begin(), track.samples.begin() + samples_per_frame);
    track.samples.erase(track.samples.begin(), track.samples.begin() + samples_per_frame);
    {
      std::lock_guard lock(mutex_);
      became_available = became_available || track.last_frame_ns == 0 ||
                         frame->captured_ns - track.last_frame_ns >= unavailable_after_ns;
      frame->sequence = ++track.sequence;
      frame->first_sample_index = track.sample_index;
      track.sample_index += monitor_frame_samples;
      track.last_frame_ns = frame->captured_ns;
      ++track.callback_count;
      track.error.clear();
    }
    callbacks_.frame(track.name, std::move(frame));
  }
  if (became_available)
    publish_status(track);
}

AudioMonitorStatus NdiAudioStreamAdapter::status(const TrackState& track) const
{
  std::lock_guard lock(mutex_);
  const auto current_ns = now_ns();
  AudioMonitorStatus result;
  result.supported = true;
  result.configured = !track.source.name.empty();
  result.available =
      track.last_frame_ns != 0 && current_ns - track.last_frame_ns < unavailable_after_ns;
  result.reason = result.available       ? std::string{}
                  : !track.error.empty() ? track.error
                  : !result.configured   ? "not-configured"
                                         : "media-negotiating";
  result.sample_rate = monitor_sample_rate;
  result.channels = monitor_channels;
  result.age_ms = track.last_frame_ns == 0 ? 0 : (current_ns - track.last_frame_ns) / 1'000'000;
  result.dropped_frames = track.dropped_frames;
  result.callback_count = track.callback_count;
  return result;
}

void NdiAudioStreamAdapter::publish_status(const TrackState& track) const
{
  if (callbacks_.status)
    callbacks_.status(track.name, status(track));
}

vmix::AudioStreamAvailability NdiAudioStreamAdapter::availability() const
{
  return {status(monitor_).available, status(master_).available};
}

} // namespace studio_remote::windows
