#pragma once

#include <studio_remote/input/hotkey.hpp>

#include <cstdint>
#include <vector>

namespace studio_remote::macos
{
struct NativeKeyboardEvent
{
  std::uint16_t key_code;
  std::uint64_t flags;
  bool key_down;
};

/// Maps a bounded chord to native key events without posting input. Initial flags preserve
/// already-held physical modifiers. Unsupported platform keys fail before any event is posted.
std::vector<NativeKeyboardEvent> keyboard_sequence(std::span<const HotkeyKey> keys,
                                                   std::uint64_t initial_flags = 0);
} // namespace studio_remote::macos
