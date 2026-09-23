#include "windows_realtime_audio_services.hpp"

#include <Windows.h>
#include <avrt.h>
#include <evntprov.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace studio_remote::windows
{
namespace
{
constexpr GUID audio_provider_id{
    0xb3747ef2, 0xd702, 0x4f98, {0xb2, 0x67, 0xc0, 0x2a, 0xa3, 0xb4, 0x24, 0x51}};

class MmcssRegistration final : public RealtimeThreadRegistration
{
public:
  explicit MmcssRegistration(RealtimeAudioRole role) noexcept
  {
    DWORD task_index = 0;
    const wchar_t* task = role == RealtimeAudioRole::ipc_reader ||
                                  role == RealtimeAudioRole::diagnostic_renderer_low_latency
                              ? L"Pro Audio"
                              : L"Audio";
    handle_ = AvSetMmThreadCharacteristicsW(task, &task_index);
    if (!handle_)
      return;
    const auto priority =
        role == RealtimeAudioRole::ipc_reader || role == RealtimeAudioRole::diagnostic_renderer
            ? AVRT_PRIORITY_NORMAL
            : AVRT_PRIORITY_HIGH;
    if (!AvSetMmThreadPriority(handle_, priority))
    {
      AvRevertMmThreadCharacteristics(handle_);
      handle_ = nullptr;
    }
  }

  ~MmcssRegistration() override
  {
    if (handle_)
      AvRevertMmThreadCharacteristics(handle_);
  }

  bool active() const noexcept override
  {
    return handle_ != nullptr;
  }

private:
  HANDLE handle_ = nullptr;
};

struct TimerDeleter
{
  void operator()(void* timer) const noexcept
  {
    if (!CloseHandle(timer))
      OutputDebugStringW(L"Could not close audio deadline timer.\n");
  }
};

} // namespace

struct WindowsRealtimeAudioServices::Impl
{
  Impl()
  {
    LARGE_INTEGER value{};
    if (QueryPerformanceFrequency(&value))
      frequency = static_cast<std::uint64_t>(value.QuadPart);
    PROCESS_POWER_THROTTLING_STATE qos{};
    qos.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    qos.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    qos.StateMask = 0;
    high_qos = SetProcessInformation(
                   GetCurrentProcess(), ProcessPowerThrottling, &qos, sizeof(qos)) != FALSE;
    EventRegister(&audio_provider_id, nullptr, nullptr, &provider);
  }

  ~Impl()
  {
    if (provider)
      EventUnregister(provider);
  }

  std::uint64_t frequency = 0;
  REGHANDLE provider = 0;
  bool high_qos = false;
};

WindowsRealtimeAudioServices::WindowsRealtimeAudioServices() : impl_(std::make_unique<Impl>())
{
}

WindowsRealtimeAudioServices::~WindowsRealtimeAudioServices() = default;

std::unique_ptr<RealtimeThreadRegistration>
WindowsRealtimeAudioServices::register_current_thread(RealtimeAudioRole role) noexcept
{
  try
  {
    return std::make_unique<MmcssRegistration>(role);
  }
  catch (...)
  {
    return {};
  }
}

std::uint64_t WindowsRealtimeAudioServices::performance_counter() const noexcept
{
  LARGE_INTEGER value{};
  return QueryPerformanceCounter(&value) ? static_cast<std::uint64_t>(value.QuadPart) : 0;
}

std::uint64_t WindowsRealtimeAudioServices::performance_frequency() const noexcept
{
  return impl_->frequency;
}

std::uint64_t WindowsRealtimeAudioServices::current_thread_cycles() const noexcept
{
  ULONG64 cycles = 0;
  return QueryThreadCycleTime(GetCurrentThread(), &cycles) ? cycles : 0;
}

bool WindowsRealtimeAudioServices::wait_until(std::uint64_t performance_counter_deadline) noexcept
{
  if (impl_->frequency == 0)
    return false;
  const auto now = performance_counter();
  if (now >= performance_counter_deadline)
    return true;
  const auto remaining = performance_counter_deadline - now;
  // Each outstanding wait owns its timer. A second audio worker cannot replace its deadline.
  const std::unique_ptr<void, TimerDeleter> timer(CreateWaitableTimerExW(
      nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE));
  if (!timer)
    return false;
  // Convert before multiplication and bound to the existing 100 ms wait, without uint64 overflow.
  const auto interval = static_cast<long double>(remaining) / impl_->frequency * 10'000'000.0L;
  LARGE_INTEGER due{};
  due.QuadPart = -static_cast<LONGLONG>(std::ceil(std::clamp(interval, 1.0L, 1'000'000.0L)));
  if (!SetWaitableTimerEx(timer.get(), &due, 0, nullptr, nullptr, nullptr, 0))
    return false;
  return WaitForSingleObject(timer.get(), 100) == WAIT_OBJECT_0 &&
         performance_counter() >= performance_counter_deadline;
}

void WindowsRealtimeAudioServices::mark_audio_state(std::string_view state) noexcept
{
  if (!impl_->provider)
    return;
  constexpr std::wstring_view prefix = L"audio_monitor_state=";
  std::array<wchar_t, 64> message{};
  std::copy(prefix.begin(), prefix.end(), message.begin());
  const auto count = std::min(state.size(), message.size() - prefix.size() - 1);
  for (std::size_t index = 0; index < count; ++index)
    message[prefix.size() + index] = static_cast<wchar_t>(state[index]);
  EventWriteString(impl_->provider, 0, 0, message.data());
}

bool WindowsRealtimeAudioServices::high_qos_enabled() const noexcept
{
  return impl_->high_qos;
}

} // namespace studio_remote::windows
