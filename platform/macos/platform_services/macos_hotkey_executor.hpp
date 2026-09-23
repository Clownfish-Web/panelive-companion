#pragma once

#include <studio_remote/input/hotkey.hpp>

#include <mutex>

namespace studio_remote::macos
{
/// Explicit system-shortcut commands only. Never requests native privacy permission or listens
/// to input. Concurrent commands serialize complete chords; unavailable permission fails closed.
class MacosHotkeyExecutor final : public IHotkeyExecutor
{
public:
  bool available() const noexcept override;
  void trigger(std::span<const HotkeyKey> keys) override;

private:
  std::mutex posting_mutex_;
};
} // namespace studio_remote::macos
