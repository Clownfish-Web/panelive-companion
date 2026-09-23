#include "macos_audio_output_endpoint.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/HostTime.h>
#include <CoreFoundation/CoreFoundation.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <syslog.h>
#include <type_traits>

namespace studio_remote::macos
{
namespace
{
constexpr std::size_t samples_per_buffer = 960 * 2;
void require(OSStatus result, const char* operation)
{
  if (result != noErr)
    throw std::runtime_error(std::string(operation) + " (AudioQueue error " +
                             std::to_string(result) + ")");
}
struct QueueDeleter
{
  void operator()(AudioQueueRef queue) const noexcept
  {
    if (AudioQueueDispose(queue, true) != noErr)
      syslog(LOG_ERR, "Could not dispose diagnostic audio output");
  }
};
using Queue = std::unique_ptr<std::remove_pointer_t<AudioQueueRef>, QueueDeleter>;

void require_device(CFStringRef uid)
{
  const AudioObjectPropertyAddress property{kAudioHardwarePropertyTranslateUIDToDevice,
                                            kAudioObjectPropertyScopeGlobal,
                                            kAudioObjectPropertyElementMain};
  AudioDeviceID device = kAudioObjectUnknown;
  UInt32 size = sizeof(device);
  require(AudioObjectGetPropertyData(
              kAudioObjectSystemObject, &property, sizeof(uid), &uid, &size, &device),
          "Resolving the selected audio output");
  if (size != sizeof(device) || device == kAudioObjectUnknown)
    throw std::runtime_error("The selected audio output is unavailable");
}
} // namespace

struct MacosAudioOutputEndpoint::Impl
{
  struct Slot
  {
    AudioQueueBufferRef buffer = nullptr; // Owned by queue.
    std::atomic<std::size_t> frames{0};
  };
  static_assert(std::atomic<std::size_t>::is_always_lock_free);

  static void consumed(void*, AudioQueueRef, AudioQueueBufferRef buffer) noexcept
  {
    auto& slot = *static_cast<Slot*>(buffer->mUserData);
    slot.frames.store(0, std::memory_order_release);
  }

  explicit Impl(std::string_view device_uid)
  {
    if (device_uid.empty())
      throw std::invalid_argument("Select an audio output device");
    if (device_uid.size() > 16'384 || device_uid.find('\0') != std::string_view::npos)
      throw std::invalid_argument("The selected audio device identifier is invalid");
    std::unique_ptr<std::remove_pointer_t<CFStringRef>, decltype(&CFRelease)> uid(
        CFStringCreateWithBytes(nullptr,
                                reinterpret_cast<const UInt8*>(device_uid.data()),
                                static_cast<CFIndex>(device_uid.size()),
                                kCFStringEncodingUTF8,
                                false),
        CFRelease);
    if (!uid)
      throw std::invalid_argument("The selected audio device identifier is invalid");
    require_device(uid.get());
    AudioStreamBasicDescription format{};
    format.mSampleRate = 48'000;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    format.mBytesPerPacket = format.mBytesPerFrame = 2 * sizeof(float);
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;
    AudioQueueRef created = nullptr;
    const auto result =
        AudioQueueNewOutput(&format, consumed, nullptr, nullptr, nullptr, 0, &created);
    queue.reset(created);
    require(result, "Creating diagnostic audio output");
    const auto device = uid.get();
    require(AudioQueueSetProperty(
                queue.get(), kAudioQueueProperty_CurrentDevice, &device, sizeof(device)),
            "Opening the selected audio output");
    const UInt32 enabled = 1;
    require(AudioQueueSetProperty(
                queue.get(), kAudioQueueProperty_EnableTimePitch, &enabled, sizeof(enabled)),
            "Enabling audio clock adjustment");
    const UInt32 algorithm = kAudioQueueTimePitchAlgorithm_Varispeed;
    require(AudioQueueSetProperty(
                queue.get(), kAudioQueueProperty_TimePitchAlgorithm, &algorithm, sizeof(algorithm)),
            "Selecting audio clock adjustment");
    for (auto& slot : slots)
    {
      require(
          AudioQueueAllocateBuffer(queue.get(), samples_per_buffer * sizeof(float), &slot.buffer),
          "Allocating diagnostic audio buffer");
      slot.buffer->mUserData = &slot;
    }
  }

  std::array<Slot, 4> slots;
  bool running = false;
  // Destroy first: synchronous disposal ends callbacks and releases queue-owned buffers.
  Queue queue;
};

MacosAudioOutputEndpoint::MacosAudioOutputEndpoint(std::string_view device_uid)
    : impl_(std::make_unique<Impl>(device_uid))
{
}
MacosAudioOutputEndpoint::~MacosAudioOutputEndpoint() = default;

bool MacosAudioOutputEndpoint::submit(std::span<const float> samples)
{
  if (samples.empty() || samples.size() > samples_per_buffer || samples.size() % 2 != 0)
    throw std::invalid_argument("Invalid diagnostic audio frame");
  for (const auto sample : samples)
    if (!std::isfinite(sample))
      throw std::invalid_argument("Invalid diagnostic audio sample");
  for (auto& slot : impl_->slots)
  {
    std::size_t available = 0;
    if (!slot.frames.compare_exchange_strong(
            available, samples.size() / 2, std::memory_order_acq_rel))
      continue;
    const auto bytes = samples.size_bytes();
    std::memcpy(slot.buffer->mAudioData, samples.data(), bytes);
    slot.buffer->mAudioDataByteSize = static_cast<UInt32>(bytes);
    const auto result = AudioQueueEnqueueBuffer(impl_->queue.get(), slot.buffer, 0, nullptr);
    if (result != noErr)
      slot.frames.store(0, std::memory_order_release);
    require(result, "Submitting diagnostic audio buffer");
    return true;
  }
  return false;
}
void MacosAudioOutputEndpoint::start()
{
  if (impl_->running)
    return;
  require(AudioQueueStart(impl_->queue.get(), nullptr), "Starting diagnostic audio output");
  impl_->running = true;
}
void MacosAudioOutputEndpoint::stop()
{
  require(AudioQueueStop(impl_->queue.get(), true), "Stopping diagnostic audio output");
  impl_->running = false;
  for (auto& slot : impl_->slots)
    slot.frames.store(0, std::memory_order_release);
}
void MacosAudioOutputEndpoint::set_rate_adjustment(double parts_per_million)
{
  if (!std::isfinite(parts_per_million) || std::abs(parts_per_million) > 250.0)
    throw std::invalid_argument("Invalid diagnostic audio rate adjustment");
  require(AudioQueueSetParameter(impl_->queue.get(),
                                 kAudioQueueParam_PlayRate,
                                 static_cast<Float32>(1.0 + parts_per_million / 1'000'000.0)),
          "Adjusting diagnostic audio clock");
}
std::size_t MacosAudioOutputEndpoint::pending_buffer_frames() const noexcept
{
  std::size_t frames = 0;
  for (const auto& slot : impl_->slots)
    frames += slot.frames.load(std::memory_order_acquire);
  return frames;
}

AudioOutputFormat MacosAudioOutputEndpoint::device_format() const
{
  AudioOutputFormat result;
  UInt32 size = sizeof(result.sample_rate);
  require(AudioQueueGetProperty(
              impl_->queue.get(), kAudioQueueDeviceProperty_SampleRate, &result.sample_rate, &size),
          "Reading the endpoint mix sample rate");
  if (size != sizeof(result.sample_rate) || !std::isfinite(result.sample_rate) ||
      result.sample_rate <= 0 || result.sample_rate > std::numeric_limits<std::uint32_t>::max())
    throw std::runtime_error("Invalid audio endpoint sample rate");
  size = sizeof(result.channels);
  require(
      AudioQueueGetProperty(
          impl_->queue.get(), kAudioQueueDeviceProperty_NumberChannels, &result.channels, &size),
      "Reading the endpoint mix channels");
  if (size != sizeof(result.channels) || result.channels == 0)
    throw std::runtime_error("Invalid audio endpoint channel count");
  return result;
}

std::optional<AudioEndpointClock> MacosAudioOutputEndpoint::clock() const
{
  if (!impl_->running)
    return std::nullopt;
  AudioTimeStamp timestamp{};
  const auto required = kAudioTimeStampSampleTimeValid | kAudioTimeStampHostTimeValid;
  if (AudioQueueDeviceGetCurrentTime(impl_->queue.get(), &timestamp) != noErr ||
      (timestamp.mFlags & required) != required || !std::isfinite(timestamp.mSampleTime) ||
      timestamp.mSampleTime < 0 || timestamp.mSampleTime >= std::ldexp(1.0, 64) ||
      timestamp.mHostTime == 0)
    return std::nullopt;
  const auto format = device_format();
  if (std::trunc(format.sample_rate) != format.sample_rate)
    return std::nullopt;
  return AudioEndpointClock{static_cast<std::uint64_t>(timestamp.mSampleTime),
                            AudioConvertHostTimeToNanos(timestamp.mHostTime) / 100,
                            static_cast<std::uint64_t>(format.sample_rate)};
}
} // namespace studio_remote::macos
