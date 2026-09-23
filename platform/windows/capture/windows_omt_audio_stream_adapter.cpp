#include "windows_omt_audio_stream_adapter.hpp"

#include <studio_remote/common/types.hpp>

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

} // namespace

OmtAudioStreamAdapter::OmtAudioStreamAdapter(std::shared_ptr<IOmtRuntime> runtime,
                                             OmtSourceDescriptor monitor_source,
                                             OmtSourceDescriptor master_source,
                                             vmix::AudioStreamAdapterCallbacks callbacks)
    : runtime_(std::move(runtime)), callbacks_(std::move(callbacks))
{
  if (!runtime_)
    throw std::invalid_argument("The OMT runtime is required");
  if (monitor_source.address.empty())
    throw std::invalid_argument("The OMT Headphones source is required");
  if (!callbacks_.frame)
    throw std::invalid_argument("The vMix audio frame callback is required");
  monitor_.source = std::move(monitor_source);
  master_.source = std::move(master_source);
}

OmtAudioStreamAdapter::~OmtAudioStreamAdapter()
{
  stop();
}

void OmtAudioStreamAdapter::start()
{
  if (started_)
    return;
  stopping_.store(false);
  started_ = true;
  monitor_.worker = std::thread([this] { receive_loop(monitor_); });
  if (!master_.source.address.empty())
    master_.worker = std::thread([this] { receive_loop(master_); });
  publish_status(monitor_);
  publish_status(master_);
}

void OmtAudioStreamAdapter::stop()
{
  stopping_.store(true);
  if (monitor_.worker.joinable())
    monitor_.worker.join();
  if (master_.worker.joinable())
    master_.worker.join();
  started_ = false;
}

void OmtAudioStreamAdapter::receive_loop(TrackState& track) noexcept
{
  try
  {
    auto receiver = runtime_->create_audio_receiver(track.source.address);
    bool previously_available = false;
    while (!stopping_.load())
    {
      auto frame = receiver->receive(200);
      if (frame)
        consume_audio(track, std::move(*frame));
      const bool available = status(track).available;
      if (available != previously_available)
      {
        publish_status(track);
        previously_available = available;
      }
    }
  }
  catch (const std::exception&)
  {
    {
      std::lock_guard lock(mutex_);
      track.error = "receiver-unavailable";
    }
    publish_status(track);
  }
  catch (...)
  {
    {
      std::lock_guard lock(mutex_);
      track.error = "receiver-unavailable";
    }
    publish_status(track);
  }
}

void OmtAudioStreamAdapter::consume_audio(TrackState& track, OmtAudioFrame frame)
{
  if (frame.sample_rate != monitor_sample_rate)
  {
    std::lock_guard lock(mutex_);
    track.error = "unsupported-sample-rate";
    return;
  }
  auto normalized = normalize_omt_audio_stereo(frame);
  if (!normalized)
  {
    std::lock_guard lock(mutex_);
    track.error = "unsupported-format";
    return;
  }
  track.samples.insert(track.samples.end(), normalized->begin(), normalized->end());

  const auto samples_per_frame = monitor_frame_samples * monitor_channels;
  const auto maximum_samples = samples_per_frame * maximum_buffered_audio_frames;
  if (track.samples.size() > maximum_samples)
  {
    const auto discard = track.samples.size() - maximum_samples;
    track.samples.erase(track.samples.begin(),
                        track.samples.begin() + static_cast<std::ptrdiff_t>(discard));
    std::lock_guard lock(mutex_);
    ++track.dropped_frames;
  }

  while (track.samples.size() >= samples_per_frame)
  {
    auto output = std::make_shared<AudioMonitorFrame>();
    output->sample_rate = monitor_sample_rate;
    output->channels = monitor_channels;
    output->captured_ns = monotonic_ns();
    output->topology_generation = 1;
    output->samples.assign(track.samples.begin(),
                           track.samples.begin() + static_cast<std::ptrdiff_t>(samples_per_frame));
    track.samples.erase(track.samples.begin(),
                        track.samples.begin() + static_cast<std::ptrdiff_t>(samples_per_frame));
    {
      std::lock_guard lock(mutex_);
      output->sequence = ++track.sequence;
      output->first_sample_index = track.sample_index;
      track.sample_index += monitor_frame_samples;
      track.last_frame_ns = output->captured_ns;
      ++track.callback_count;
      track.error.clear();
    }
    callbacks_.frame(track.name, std::move(output));
  }
}

AudioMonitorStatus OmtAudioStreamAdapter::status(const TrackState& track) const
{
  std::lock_guard lock(mutex_);
  const auto current_ns = monotonic_ns();
  AudioMonitorStatus result;
  result.supported = true;
  result.configured = !track.source.address.empty();
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

void OmtAudioStreamAdapter::publish_status(const TrackState& track) const
{
  if (callbacks_.status)
    callbacks_.status(track.name, status(track));
}

vmix::AudioStreamAvailability OmtAudioStreamAdapter::availability() const
{
  return {status(monitor_).available, status(master_).available};
}

} // namespace studio_remote::windows
