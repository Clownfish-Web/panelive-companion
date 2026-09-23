#pragma once

#include <studio_remote/media/audio_monitor.hpp>

#include <cstddef>
#include <deque>
#include <utility>

namespace studio_remote
{

// This queue is intentionally not internally synchronized. Its owner can use one mutex for the
// queue and the condition variable that wakes its single consumer.
class BoundedAudioFrameQueue
{
public:
  explicit BoundedAudioFrameQueue(std::size_t capacity) : capacity_(capacity)
  {
  }

  std::uint64_t push(SharedAudioMonitorFrame frame)
  {
    if (!frame || capacity_ == 0)
      return 0;
    std::uint64_t dropped = 0;
    if (frames_.size() == capacity_)
    {
      frames_.pop_front();
      dropped = 1;
    }
    frames_.push_back(std::move(frame));
    return dropped;
  }

  SharedAudioMonitorFrame pop()
  {
    if (frames_.empty())
      return {};
    auto frame = std::move(frames_.front());
    frames_.pop_front();
    return frame;
  }

  void clear() noexcept
  {
    frames_.clear();
  }

  bool empty() const noexcept
  {
    return frames_.empty();
  }

  std::size_t size() const noexcept
  {
    return frames_.size();
  }

private:
  std::size_t capacity_;
  std::deque<SharedAudioMonitorFrame> frames_;
};

} // namespace studio_remote
