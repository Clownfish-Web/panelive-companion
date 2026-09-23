#pragma once

#include <functional>
#include <string_view>
#include <studio_remote/media/audio_monitor.hpp>

namespace studio_remote::vmix
{

struct AudioStreamAvailability
{
  bool monitor = false;
  bool master = false;
};

struct AudioStreamAdapterCallbacks
{
  using FrameHandler = std::function<void(std::string_view mix, SharedAudioMonitorFrame frame)>;
  using StatusHandler = std::function<void(std::string_view mix, const AudioMonitorStatus& status)>;

  FrameHandler frame;
  StatusHandler status;
};

// Owns vMix monitoring-audio transport only. Implementations publish normalized interleaved float
// PCM and per-mix status without owning WebRTC delivery or vMix control state.
class IVmixAudioStreamAdapter
{
public:
  virtual ~IVmixAudioStreamAdapter() = default;

  virtual void start() = 0;
  virtual void stop() = 0;
  virtual AudioStreamAvailability availability() const = 0;
};

} // namespace studio_remote::vmix
