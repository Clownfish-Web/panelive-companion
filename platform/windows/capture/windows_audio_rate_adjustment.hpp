#pragma once

#include <cstdint>
#include <memory>

struct IAudioClockAdjustment;

namespace studio_remote::windows
{

// Applies endpoint rate changes away from the WASAPI render-event thread.
class WindowsAudioRateAdjustment
{
public:
  explicit WindowsAudioRateAdjustment(IAudioClockAdjustment* adjustment);
  ~WindowsAudioRateAdjustment();

  WindowsAudioRateAdjustment(const WindowsAudioRateAdjustment&) = delete;
  WindowsAudioRateAdjustment& operator=(const WindowsAudioRateAdjustment&) = delete;

  void request(double sample_rate) noexcept;
  std::uint64_t successes() const noexcept;
  std::uint64_t failures() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace studio_remote::windows
