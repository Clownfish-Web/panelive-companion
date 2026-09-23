#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace studio_remote
{
enum class ClientDeliveryPriority
{
  control,
  state
};

/// Ordered canonical replacements share one lane: newer tally must not overtake older resources.
/// Shared immutable payloads avoid copying provider broadcasts for each recipient.
class ClientDeliveryQueue
{
public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::size_t maximum_messages = 1024;
  static constexpr std::size_t maximum_bytes = 8 * 1024 * 1024;
  bool push(std::shared_ptr<const std::string> message,
            ClientDeliveryPriority priority,
            Clock::time_point now = Clock::now());
  std::shared_ptr<const std::string> pop(Clock::time_point now = Clock::now());
  bool failed() const;

private:
  struct Delivery
  {
    std::shared_ptr<const std::string> message;
    Clock::time_point queued_at;
  };
  void fail_locked();
  mutable std::mutex mutex_;
  std::array<std::deque<Delivery>, 2> lanes_;
  std::size_t messages_ = 0;
  std::size_t bytes_ = 0;
  bool failed_ = false;
};
} // namespace studio_remote
