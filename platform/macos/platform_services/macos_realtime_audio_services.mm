#include "macos_realtime_audio_services.hpp"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <array>
#include <limits>
#include <mach/mach_time.h>
#include <new>
#include <pthread/qos.h>
#include <syslog.h>
#include <time.h>

namespace studio_remote::macos
{
namespace
{
class QosRegistration final : public RealtimeThreadRegistration
{
public:
  explicit QosRegistration(RealtimeAudioRole role) noexcept
  {
    const auto qos = role == RealtimeAudioRole::ipc_reader ||
                             role == RealtimeAudioRole::diagnostic_renderer_low_latency
                         ? QOS_CLASS_USER_INTERACTIVE
                         : QOS_CLASS_USER_INITIATED;
    override_ = pthread_override_qos_class_start_np(pthread_self(), qos, 0);
    if (!override_)
      syslog(LOG_ERR, "Could not register audio thread QoS");
  }
  ~QosRegistration() override
  {
    if (override_ && pthread_override_qos_class_end_np(override_) != 0)
      syslog(LOG_ERR, "Could not release audio thread QoS");
  }
  bool active() const noexcept override
  {
    return override_ != nullptr;
  }

private:
  pthread_override_t override_ = nullptr;
};
} // namespace

struct MacosRealtimeAudioServices::Impl
{
  Impl()
  {
    if (mach_timebase_info(&timebase) != KERN_SUCCESS || timebase.numer == 0 || timebase.denom == 0)
    {
      timebase = {};
      syslog(LOG_ERR, "Could not read audio performance timebase");
    }
    @autoreleasepool
    {
      activity = [[NSProcessInfo processInfo]
          beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep |
                                   NSActivityLatencyCritical
                            reason:@"OBS Companion audio processing"];
    }
    if (!activity)
      syslog(LOG_ERR, "Could not start audio processing activity");
  }
  ~Impl()
  {
    @autoreleasepool
    {
      if (activity)
        [[NSProcessInfo processInfo] endActivity:activity];
    }
  }

  mach_timebase_info_data_t timebase{};
  __strong id activity = nil;
};

MacosRealtimeAudioServices::MacosRealtimeAudioServices() : impl_(std::make_unique<Impl>())
{
}
MacosRealtimeAudioServices::~MacosRealtimeAudioServices() = default;

std::unique_ptr<RealtimeThreadRegistration>
MacosRealtimeAudioServices::register_current_thread(RealtimeAudioRole role) noexcept
{
  try
  {
    return std::make_unique<QosRegistration>(role);
  }
  catch (const std::bad_alloc&)
  {
    syslog(LOG_ERR, "Could not allocate audio thread QoS registration");
    return {};
  }
}

std::uint64_t MacosRealtimeAudioServices::performance_counter() const noexcept
{
  return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}
std::uint64_t MacosRealtimeAudioServices::performance_frequency() const noexcept
{
  return 1'000'000'000ULL;
}
std::uint64_t MacosRealtimeAudioServices::current_thread_cycles() const noexcept
{
  // macOS has no corresponding supported public hardware-cycle query. The shared diagnostics
  // already use zero for an unavailable counter; CPU time must not be mislabeled as cycles.
  return 0;
}

bool MacosRealtimeAudioServices::wait_until(std::uint64_t deadline) noexcept
{
  const auto now = performance_counter();
  if (now >= deadline)
    return true;
  const auto& timebase = impl_->timebase;
  if (timebase.numer == 0 || timebase.denom == 0)
    return false;
  // Preserve Windows' maximum 100 ms wait. Each caller waits independently, without a shared
  // timer whose deadline could be replaced by another audio thread.
  const auto bounded = now + std::min<std::uint64_t>(deadline - now, 100'000'000ULL);
  const auto whole = bounded / timebase.numer;
  const auto remainder = bounded % timebase.numer;
  const auto fractional = (remainder * timebase.denom + timebase.numer - 1) / timebase.numer;
  if (whole > (std::numeric_limits<std::uint64_t>::max() - fractional) / timebase.denom)
    return false;
  const auto ticks = whole * timebase.denom + fractional;
  return mach_wait_until(ticks) == KERN_SUCCESS && performance_counter() >= deadline;
}

void MacosRealtimeAudioServices::mark_audio_state(std::string_view state) noexcept
{
  std::array<char, 44> message{};
  const auto count = std::min(state.size(), message.size() - 1);
  for (std::size_t index = 0; index < count; ++index)
    message[index] = state[index] >= 32 && state[index] <= 126 ? state[index] : '?';
  syslog(LOG_DEBUG, "audio_monitor_state=%s", message.data());
}
bool MacosRealtimeAudioServices::high_qos_enabled() const noexcept
{
  return impl_->activity != nil;
}
} // namespace studio_remote::macos
