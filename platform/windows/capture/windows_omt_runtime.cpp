#include "windows_omt_runtime.hpp"

#include <libomt.h>

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace studio_remote::windows
{
namespace
{

constexpr std::uint32_t maximum_sample_rate = 384'000;
constexpr std::uint32_t maximum_channels = 32;
constexpr std::uint32_t maximum_samples_per_channel = 32'768;

std::string ascii_lower(std::string_view value)
{
  std::string result(value);
  std::transform(result.begin(),
                 result.end(),
                 result.begin(),
                 [](unsigned char character)
                 { return static_cast<char>(std::tolower(character)); });
  return result;
}

bool matches_host(const OmtSourceDescriptor& source, std::string_view host)
{
  if (host.empty())
    return false;
  const auto normalized_host = ascii_lower(host);
  const auto normalized_source_host = ascii_lower(source.host);
  return normalized_source_host == normalized_host ||
         normalized_source_host.starts_with(normalized_host + ".") ||
         normalized_host.starts_with(normalized_source_host + ".");
}

bool is_loopback(std::string_view host)
{
  const auto normalized = ascii_lower(host);
  return normalized == "localhost" || normalized == "127.0.0.1" || normalized == "::1";
}

std::optional<OmtSourceDescriptor>
select_vmix_omt_audio_source(const std::vector<std::string>& addresses,
                             std::string_view label,
                             std::string_view configured_host,
                             std::string_view local_hostname)
{
  const auto normalized_label = ascii_lower(label);
  std::vector<OmtSourceDescriptor> candidates;
  for (const auto& address : addresses)
  {
    auto source = parse_omt_source(address);
    if (source && ascii_lower(source->label).find(normalized_label) != std::string::npos)
      candidates.push_back(std::move(*source));
  }
  if (candidates.size() == 1)
    return candidates.front();

  std::vector<OmtSourceDescriptor> preferred;
  for (const auto& source : candidates)
  {
    const bool configured_match = matches_host(source, configured_host);
    const bool local_match = is_loopback(configured_host) && matches_host(source, local_hostname);
    if (configured_match || local_match)
      preferred.push_back(source);
  }
  return preferred.size() == 1 ? std::optional<OmtSourceDescriptor>{preferred.front()}
                               : std::nullopt;
}

} // namespace

std::optional<OmtSourceDescriptor> parse_omt_source(std::string_view address)
{
  const auto open = address.find('(');
  const auto close = address.rfind(')');
  if (open == std::string_view::npos || close == std::string_view::npos || close <= open + 1)
    return std::nullopt;

  auto host = address.substr(0, open);
  while (!host.empty() && std::isspace(static_cast<unsigned char>(host.back())) != 0)
    host.remove_suffix(1);
  while (!host.empty() && std::isspace(static_cast<unsigned char>(host.front())) != 0)
    host.remove_prefix(1);
  if (host.empty())
    return std::nullopt;

  return OmtSourceDescriptor{std::string(address),
                             std::string(host),
                             std::string(address.substr(open + 1, close - open - 1))};
}

std::string local_omt_hostname()
{
  char hostname[MAX_COMPUTERNAME_LENGTH + 1]{};
  DWORD length = MAX_COMPUTERNAME_LENGTH + 1;
  return GetComputerNameA(hostname, &length) ? std::string(hostname, length) : std::string{};
}

std::optional<OmtSourceDescriptor>
select_vmix_omt_headphones_source(const std::vector<std::string>& addresses,
                                  std::string_view configured_host,
                                  std::string_view local_hostname)
{
  return select_vmix_omt_audio_source(addresses, "headphones", configured_host, local_hostname);
}

std::optional<OmtSourceDescriptor>
select_vmix_omt_master_source(const std::vector<std::string>& addresses,
                              std::string_view configured_host,
                              std::string_view local_hostname)
{
  return select_vmix_omt_audio_source(addresses, "master", configured_host, local_hostname);
}

std::optional<std::vector<float>> normalize_omt_audio_stereo(const OmtAudioFrame& frame)
{
  if (frame.sample_rate == 0 || frame.sample_rate > maximum_sample_rate || frame.channels == 0 ||
      frame.channels > maximum_channels || frame.samples_per_channel == 0 ||
      frame.samples_per_channel > maximum_samples_per_channel)
    return std::nullopt;

  const auto expected = static_cast<std::size_t>(frame.channels) * frame.samples_per_channel;
  if (frame.planar_samples.size() != expected)
    return std::nullopt;

  std::vector<float> result;
  result.reserve(static_cast<std::size_t>(frame.samples_per_channel) * 2);
  const auto right_offset = frame.channels > 1 ? frame.samples_per_channel : 0;
  for (std::uint32_t index = 0; index < frame.samples_per_channel; ++index)
  {
    const auto left = frame.planar_samples[index];
    const auto right = frame.planar_samples[right_offset + index];
    result.push_back(std::isfinite(left) ? left : 0.0F);
    result.push_back(std::isfinite(right) ? right : 0.0F);
  }
  return result;
}

struct OmtAudioReceiver::State
{
  omt_receive_t* receiver = nullptr;
};

OmtAudioReceiver::OmtAudioReceiver(std::string address) : state_(std::make_unique<State>())
{
  state_->receiver = omt_receive_create(
      address.c_str(), OMTFrameType_Audio, OMTPreferredVideoFormat_UYVY, OMTReceiveFlags_None);
  if (!state_->receiver)
    throw std::runtime_error("Could not create the OMT audio receiver");
}

OmtAudioReceiver::~OmtAudioReceiver()
{
  if (state_ && state_->receiver)
    omt_receive_destroy(state_->receiver);
}

std::optional<OmtAudioFrame> OmtAudioReceiver::receive(std::uint32_t timeout_ms)
{
  const auto bounded_timeout = static_cast<int>(std::min<std::uint32_t>(timeout_ms, 1'000));
  auto* frame = omt_receive(state_->receiver, OMTFrameType_Audio, bounded_timeout);
  if (!frame)
    return std::nullopt;
  if (frame->Type != OMTFrameType_Audio || frame->Codec != OMTCodec_FPA1 ||
      frame->SampleRate <= 0 || frame->Channels <= 0 || frame->SamplesPerChannel <= 0 ||
      !frame->Data || frame->DataLength < 0)
    return std::nullopt;

  const auto channels = static_cast<std::uint32_t>(frame->Channels);
  const auto samples_per_channel = static_cast<std::uint32_t>(frame->SamplesPerChannel);
  if (channels > maximum_channels || samples_per_channel > maximum_samples_per_channel)
    return std::nullopt;
  const auto sample_count = static_cast<std::uint64_t>(channels) * samples_per_channel;
  const auto byte_count = sample_count * sizeof(float);
  if (byte_count > static_cast<std::uint64_t>(frame->DataLength) ||
      byte_count > std::numeric_limits<std::size_t>::max())
    return std::nullopt;

  const auto* samples = static_cast<const float*>(frame->Data);
  OmtAudioFrame result;
  result.sample_rate = static_cast<std::uint32_t>(frame->SampleRate);
  result.channels = channels;
  result.samples_per_channel = samples_per_channel;
  result.planar_samples.assign(samples, samples + static_cast<std::size_t>(sample_count));
  return result;
}

OmtRuntime::OmtRuntime()
{
  // The Companion owns diagnostics and must not let the library create an implicit global log.
  omt_setloggingfilename(nullptr);
}

OmtRuntime::~OmtRuntime() = default;

std::vector<std::string> OmtRuntime::discover_sources() const
{
  int count = 0;
  auto** addresses = omt_discovery_getaddresses(&count);
  std::vector<std::string> result;
  if (!addresses || count <= 0)
    return result;
  result.reserve(static_cast<std::size_t>(count));
  for (int index = 0; index < count; ++index)
    if (addresses[index] && addresses[index][0] != '\0')
      result.emplace_back(addresses[index]);
  return result;
}

std::unique_ptr<IOmtAudioReceiver> OmtRuntime::create_audio_receiver(std::string address) const
{
  return std::make_unique<OmtAudioReceiver>(std::move(address));
}

std::shared_ptr<OmtRuntime> OmtRuntimeProvider::acquire()
{
  std::lock_guard lock(mutex_);
  if (!runtime_)
    runtime_ = std::make_shared<OmtRuntime>();
  return runtime_;
}

} // namespace studio_remote::windows
