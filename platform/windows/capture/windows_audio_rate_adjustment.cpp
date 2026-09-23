#include "windows_audio_rate_adjustment.hpp"

#include <Windows.h>
#include <audioclient.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace studio_remote::windows
{

struct WindowsAudioRateAdjustment::Impl
{
  explicit Impl(IAudioClockAdjustment* value) : adjustment(value), thread([this] { run(); })
  {
  }

  ~Impl()
  {
    stopping.store(true, std::memory_order_release);
    changed.notify_all();
    if (thread.joinable())
      thread.join();
  }

  void run() noexcept
  {
    const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE)
    {
      failures.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    double applied_rate = 48'000.0;
    std::unique_lock wait_lock(wait_mutex);
    while (!stopping.load(std::memory_order_acquire))
    {
      changed.wait_for(wait_lock, std::chrono::milliseconds(250));
      const auto requested = requested_rate.load(std::memory_order_acquire);
      if (stopping.load(std::memory_order_acquire))
        break;
      if (std::abs(requested - applied_rate) < 0.01)
        continue;
      if (SUCCEEDED(adjustment->SetSampleRate(static_cast<float>(requested))))
      {
        applied_rate = requested;
        successes.fetch_add(1, std::memory_order_relaxed);
      }
      else
      {
        failures.fetch_add(1, std::memory_order_relaxed);
      }
    }
    if (com_result == S_OK || com_result == S_FALSE)
      CoUninitialize();
  }

  Microsoft::WRL::ComPtr<IAudioClockAdjustment> adjustment;
  std::atomic<bool> stopping{false};
  std::atomic<double> requested_rate{48'000.0};
  std::atomic<std::uint64_t> successes{0};
  std::atomic<std::uint64_t> failures{0};
  std::mutex wait_mutex;
  std::condition_variable changed;
  std::thread thread;
};

WindowsAudioRateAdjustment::WindowsAudioRateAdjustment(IAudioClockAdjustment* adjustment)
    : impl_(std::make_unique<Impl>(adjustment))
{
}

WindowsAudioRateAdjustment::~WindowsAudioRateAdjustment() = default;

void WindowsAudioRateAdjustment::request(double sample_rate) noexcept
{
  impl_->requested_rate.store(sample_rate, std::memory_order_release);
  impl_->changed.notify_one();
}

std::uint64_t WindowsAudioRateAdjustment::successes() const noexcept
{
  return impl_->successes.load(std::memory_order_relaxed);
}

std::uint64_t WindowsAudioRateAdjustment::failures() const noexcept
{
  return impl_->failures.load(std::memory_order_relaxed);
}

} // namespace studio_remote::windows
