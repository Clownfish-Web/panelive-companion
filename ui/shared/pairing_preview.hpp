#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace studio_remote
{
/// UI-thread presentation state only. Magnification never changes the underlying pairing lease.
class PairingPreview
{
public:
  using TimePoint = std::chrono::steady_clock::time_point;
  static constexpr int normal_size = 180;
  static constexpr int enlarged_size = normal_size * 3;

  void open(std::span<const std::uint8_t> pixels,
            std::uint64_t expires,
            TimePoint now,
            std::uint64_t unix_seconds);
  [[nodiscard]] int seconds_remaining(std::span<const std::uint8_t> pixels,
                                      std::uint64_t expires,
                                      TimePoint now,
                                      std::uint64_t unix_seconds);
  void close() noexcept;

private:
  struct Active
  {
    std::vector<std::uint8_t> pixels;
    std::uint64_t expires;
    TimePoint deadline;
  };
  std::optional<Active> active_;
};
} // namespace studio_remote
