#pragma once

#include <studio_remote/input/hotkey.hpp>

#include <Windows.h>
#include <functional>

namespace studio_remote::windows
{
class WindowsHotkeyExecutor final : public IHotkeyExecutor
{
public:
  using InputSender = std::function<UINT(std::span<INPUT>)>;

  WindowsHotkeyExecutor();
  explicit WindowsHotkeyExecutor(InputSender sender);
  bool available() const noexcept override;
  void trigger(std::span<const HotkeyKey> keys) override;

private:
  InputSender sender_;
};
} // namespace studio_remote::windows
