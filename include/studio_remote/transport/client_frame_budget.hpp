#pragma once

#include <chrono>
#include <cstddef>
#include <optional>

namespace studio_remote
{
/// Receive-thread-owned ingress budget. No locks, allocation, callbacks or protocol parsing.
/// Charge complete frames before application parsing; transport allocation has its own size cap.
class ClientFrameBudget
{
public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::size_t frames_per_second = 128;
  static constexpr std::size_t frame_burst = 256;
  static constexpr std::size_t bytes_per_second = 1024 * 1024;
  static constexpr std::size_t byte_burst = 2 * bytes_per_second;

  bool admit(std::size_t payload_bytes, Clock::time_point now) noexcept;

private:
  std::optional<Clock::time_point> updated_;
  double frames_ = frame_burst;
  double bytes_ = byte_burst;
};
} // namespace studio_remote
