#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace studio_remote::ipc
{

struct VideoSlotSequenceObservation
{
  bool publish = false;
  std::uint64_t dropped_frames = 0;
};

class VideoSlotSequenceTracker
{
public:
  bool is_unseen(std::size_t slot_index, std::uint64_t sequence) const noexcept
  {
    return slot_index < last_slot_sequences_.size() && sequence != 0 &&
           sequence != last_slot_sequences_[slot_index];
  }

  VideoSlotSequenceObservation
  accept(std::size_t slot_index, std::uint64_t sequence, std::uint64_t captured_ns) noexcept
  {
    if (!is_unseen(slot_index, sequence))
      return {};

    auto& previous_sequence = last_slot_sequences_[slot_index];
    std::uint64_t dropped_frames = 0;
    if (previous_sequence != 0 && sequence > previous_sequence + 2)
      dropped_frames = (sequence - previous_sequence) / 2 - 1;
    previous_sequence = sequence;

    if (captured_ns == last_captured_ns_)
      return {false, dropped_frames};
    last_captured_ns_ = captured_ns;
    return {true, dropped_frames};
  }

  void reset_identity() noexcept
  {
    last_slot_sequences_.fill(0);
    last_captured_ns_ = 0;
  }

private:
  // Each producer slot advances its own seqlock counter. Comparing slots through one shared
  // counter makes every alternating frame appear to be a duplicate and halves delivery FPS.
  std::array<std::uint64_t, 2> last_slot_sequences_{};
  std::uint64_t last_captured_ns_ = 0;
};

} // namespace studio_remote::ipc
