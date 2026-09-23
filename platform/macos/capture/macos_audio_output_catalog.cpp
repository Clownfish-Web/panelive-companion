#include "macos_audio_output_catalog.hpp"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <syslog.h>
#include <type_traits>

namespace studio_remote::macos
{
namespace
{
void require(OSStatus status, const char* operation)
{
  if (status != noErr)
    throw std::runtime_error(std::string(operation) + " (CoreAudio error " +
                             std::to_string(status) + ")");
}

std::vector<AudioDeviceID> device_ids()
{
  const AudioObjectPropertyAddress property{kAudioHardwarePropertyDevices,
                                            kAudioObjectPropertyScopeGlobal,
                                            kAudioObjectPropertyElementMain};
  for (unsigned attempt = 0; attempt < 3; ++attempt)
  {
    UInt32 size = 0;
    require(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &property, 0, nullptr, &size),
            "Counting active audio outputs");
    if (size > 1024 * 1024 || size % sizeof(AudioDeviceID) != 0)
      throw std::runtime_error("Invalid CoreAudio device list size");
    if (size == 0)
      return {};
    std::vector<AudioDeviceID> devices(size / sizeof(AudioDeviceID));
    const auto capacity = size;
    const auto result = AudioObjectGetPropertyData(
        kAudioObjectSystemObject, &property, 0, nullptr, &size, devices.data());
    if (result == kAudioHardwareBadPropertySizeError)
      continue;
    require(result, "Enumerating active audio outputs");
    if (size > capacity || size % sizeof(AudioDeviceID) != 0)
      throw std::runtime_error("Invalid CoreAudio device list size");
    devices.resize(size / sizeof(AudioDeviceID));
    return devices;
  }
  throw std::runtime_error("Audio output devices changed during enumeration");
}

AudioDeviceID default_output()
{
  const AudioObjectPropertyAddress property{kAudioHardwarePropertyDefaultOutputDevice,
                                            kAudioObjectPropertyScopeGlobal,
                                            kAudioObjectPropertyElementMain};
  AudioDeviceID device = kAudioObjectUnknown;
  UInt32 size = sizeof(device);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &property, 0, nullptr, &size, &device) !=
          noErr ||
      size != sizeof(device))
  {
    syslog(LOG_WARNING, "Could not read default audio output");
    return kAudioObjectUnknown;
  }
  return device;
}

bool active_output(AudioDeviceID device)
{
  const AudioObjectPropertyAddress alive_property{kAudioDevicePropertyDeviceIsAlive,
                                                  kAudioObjectPropertyScopeGlobal,
                                                  kAudioObjectPropertyElementMain};
  UInt32 alive = 0;
  UInt32 size = sizeof(alive);
  const auto result =
      AudioObjectGetPropertyData(device, &alive_property, 0, nullptr, &size, &alive);
  // Devices may disappear between the system snapshot and per-device queries.
  if (result == kAudioHardwareBadObjectError || result == kAudioHardwareBadDeviceError)
    return false;
  require(result, "Reading audio output availability");
  if (size != sizeof(alive))
    throw std::runtime_error("Invalid CoreAudio device availability size");
  if (alive == 0)
    return false;
  const AudioObjectPropertyAddress streams{kAudioDevicePropertyStreams,
                                           kAudioDevicePropertyScopeOutput,
                                           kAudioObjectPropertyElementMain};
  size = 0;
  require(AudioObjectGetPropertyDataSize(device, &streams, 0, nullptr, &size),
          "Reading audio output streams");
  return size >= sizeof(AudioStreamID);
}

std::string device_text(AudioDeviceID device, AudioObjectPropertySelector selector)
{
  const AudioObjectPropertyAddress property{
      selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  CFStringRef value = nullptr;
  UInt32 size = sizeof(value);
  const auto result = AudioObjectGetPropertyData(device, &property, 0, nullptr, &size, &value);
  std::unique_ptr<std::remove_pointer_t<CFStringRef>, decltype(&CFRelease)> owner(value, CFRelease);
  if (selector == kAudioObjectPropertyName && (result != noErr || size != sizeof(value) || !value ||
                                               CFGetTypeID(value) != CFStringGetTypeID()))
  {
    syslog(LOG_WARNING, "Could not read audio output name");
    return "Unknown audio output";
  }
  require(result, "Reading audio output identity");
  if (size != sizeof(value) || !value || CFGetTypeID(value) != CFStringGetTypeID())
    throw std::runtime_error("Invalid CoreAudio device identity");
  const auto length = CFStringGetLength(value);
  if (length > 16'384)
    throw std::runtime_error("CoreAudio device identity is too long");
  for (CFIndex index = 0; index < length; ++index)
    if (CFStringGetCharacterAtIndex(value, index) == 0)
      throw std::runtime_error("Invalid CoreAudio device identity");
  const auto capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  if (capacity <= 0)
    throw std::runtime_error("Invalid CoreAudio device identity encoding");
  std::vector<char> buffer(static_cast<std::size_t>(capacity));
  if (!CFStringGetCString(value, buffer.data(), capacity, kCFStringEncodingUTF8))
    throw std::runtime_error("Invalid CoreAudio device identity encoding");
  return buffer.data();
}
} // namespace

std::vector<AudioOutputDevice> MacosAudioOutputCatalog::output_devices() const
{
  const auto devices = device_ids();
  const auto default_id = default_output();
  std::vector<AudioOutputDevice> result;
  result.reserve(devices.size());
  for (const auto device : devices)
  {
    if (!active_output(device))
      continue;
    auto id = device_text(device, kAudioDevicePropertyDeviceUID);
    if (id.empty())
      throw std::runtime_error("CoreAudio output identity is empty");
    result.push_back(
        {std::move(id), device_text(device, kAudioObjectPropertyName), device == default_id});
  }
  std::stable_sort(result.begin(),
                   result.end(),
                   [](const auto& left, const auto& right)
                   {
                     if (left.is_default != right.is_default)
                       return left.is_default;
                     return left.name < right.name;
                   });
  return result;
}
} // namespace studio_remote::macos
