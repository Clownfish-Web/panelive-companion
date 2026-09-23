#include "windows_hotkey_executor.hpp"

#include <Windows.h>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace studio_remote::windows
{
namespace
{
struct NativeKey
{
  WORD virtual_key = 0;
  DWORD flags = 0;
};

NativeKey named_key(HotkeyNamedKey key)
{
  constexpr DWORD extended = KEYEVENTF_EXTENDEDKEY;
  switch (key)
  {
  case HotkeyNamedKey::command:
    throw HotkeyExecutionError("system-hotkey-modifier-unsupported",
                               "Windows input injection does not support the Command key");
  case HotkeyNamedKey::control:
    return {VK_CONTROL};
  case HotkeyNamedKey::left_control:
    return {VK_LCONTROL};
  case HotkeyNamedKey::right_control:
    return {VK_RCONTROL, extended};
  case HotkeyNamedKey::shift:
    return {VK_SHIFT};
  case HotkeyNamedKey::left_shift:
    return {VK_LSHIFT};
  case HotkeyNamedKey::right_shift:
    return {VK_RSHIFT};
  case HotkeyNamedKey::alt:
    return {VK_MENU};
  case HotkeyNamedKey::left_alt:
    return {VK_LMENU};
  case HotkeyNamedKey::right_alt:
    return {VK_RMENU, extended};
  case HotkeyNamedKey::windows:
  case HotkeyNamedKey::left_windows:
    return {VK_LWIN, extended};
  case HotkeyNamedKey::right_windows:
    return {VK_RWIN, extended};
  case HotkeyNamedKey::escape:
    return {VK_ESCAPE};
  case HotkeyNamedKey::tab:
    return {VK_TAB};
  case HotkeyNamedKey::enter:
    return {VK_RETURN};
  case HotkeyNamedKey::space:
    return {VK_SPACE};
  case HotkeyNamedKey::backspace:
    return {VK_BACK};
  case HotkeyNamedKey::insert:
    return {VK_INSERT, extended};
  case HotkeyNamedKey::delete_key:
    return {VK_DELETE, extended};
  case HotkeyNamedKey::home:
    return {VK_HOME, extended};
  case HotkeyNamedKey::end:
    return {VK_END, extended};
  case HotkeyNamedKey::page_up:
    return {VK_PRIOR, extended};
  case HotkeyNamedKey::page_down:
    return {VK_NEXT, extended};
  case HotkeyNamedKey::left:
    return {VK_LEFT, extended};
  case HotkeyNamedKey::right:
    return {VK_RIGHT, extended};
  case HotkeyNamedKey::up:
    return {VK_UP, extended};
  case HotkeyNamedKey::down:
    return {VK_DOWN, extended};
  case HotkeyNamedKey::print_screen:
    return {VK_SNAPSHOT, extended};
  case HotkeyNamedKey::scroll_lock:
    return {VK_SCROLL};
  case HotkeyNamedKey::pause:
    return {VK_PAUSE};
  case HotkeyNamedKey::caps_lock:
    return {VK_CAPITAL};
  case HotkeyNamedKey::num_lock:
    return {VK_NUMLOCK, extended};
  case HotkeyNamedKey::context_menu:
    return {VK_APPS, extended};
  case HotkeyNamedKey::numpad_0:
  case HotkeyNamedKey::numpad_1:
  case HotkeyNamedKey::numpad_2:
  case HotkeyNamedKey::numpad_3:
  case HotkeyNamedKey::numpad_4:
  case HotkeyNamedKey::numpad_5:
  case HotkeyNamedKey::numpad_6:
  case HotkeyNamedKey::numpad_7:
  case HotkeyNamedKey::numpad_8:
  case HotkeyNamedKey::numpad_9:
    return {static_cast<WORD>(VK_NUMPAD0 + static_cast<int>(key) -
                              static_cast<int>(HotkeyNamedKey::numpad_0))};
  case HotkeyNamedKey::numpad_add:
    return {VK_ADD};
  case HotkeyNamedKey::numpad_subtract:
    return {VK_SUBTRACT};
  case HotkeyNamedKey::numpad_multiply:
    return {VK_MULTIPLY};
  case HotkeyNamedKey::numpad_divide:
    return {VK_DIVIDE, extended};
  case HotkeyNamedKey::numpad_decimal:
    return {VK_DECIMAL};
  case HotkeyNamedKey::volume_mute:
    return {VK_VOLUME_MUTE, extended};
  case HotkeyNamedKey::volume_down:
    return {VK_VOLUME_DOWN, extended};
  case HotkeyNamedKey::volume_up:
    return {VK_VOLUME_UP, extended};
  case HotkeyNamedKey::media_next:
    return {VK_MEDIA_NEXT_TRACK, extended};
  case HotkeyNamedKey::media_previous:
    return {VK_MEDIA_PREV_TRACK, extended};
  case HotkeyNamedKey::media_stop:
    return {VK_MEDIA_STOP, extended};
  case HotkeyNamedKey::media_play_pause:
    return {VK_MEDIA_PLAY_PAUSE, extended};
  case HotkeyNamedKey::browser_back:
    return {VK_BROWSER_BACK, extended};
  case HotkeyNamedKey::browser_forward:
    return {VK_BROWSER_FORWARD, extended};
  case HotkeyNamedKey::browser_refresh:
    return {VK_BROWSER_REFRESH, extended};
  case HotkeyNamedKey::browser_stop:
    return {VK_BROWSER_STOP, extended};
  case HotkeyNamedKey::browser_search:
    return {VK_BROWSER_SEARCH, extended};
  case HotkeyNamedKey::browser_favorites:
    return {VK_BROWSER_FAVORITES, extended};
  case HotkeyNamedKey::browser_home:
    return {VK_BROWSER_HOME, extended};
  case HotkeyNamedKey::semicolon:
    return {VK_OEM_1};
  case HotkeyNamedKey::equals:
    return {VK_OEM_PLUS};
  case HotkeyNamedKey::comma:
    return {VK_OEM_COMMA};
  case HotkeyNamedKey::minus:
    return {VK_OEM_MINUS};
  case HotkeyNamedKey::period:
    return {VK_OEM_PERIOD};
  case HotkeyNamedKey::slash:
    return {VK_OEM_2};
  case HotkeyNamedKey::backquote:
    return {VK_OEM_3};
  case HotkeyNamedKey::left_bracket:
    return {VK_OEM_4};
  case HotkeyNamedKey::backslash:
    return {VK_OEM_5};
  case HotkeyNamedKey::right_bracket:
    return {VK_OEM_6};
  case HotkeyNamedKey::quote:
    return {VK_OEM_7};
  }
  throw std::invalid_argument("Unsupported hotkey key");
}

NativeKey native_key(const HotkeyKey& key)
{
  if (key.kind == HotkeyKeyKind::letter && key.value < 26)
    return {static_cast<WORD>('A' + key.value)};
  if (key.kind == HotkeyKeyKind::digit && key.value < 10)
    return {static_cast<WORD>('0' + key.value)};
  if (key.kind == HotkeyKeyKind::function && key.value >= 1 && key.value <= 24)
    return {static_cast<WORD>(VK_F1 + key.value - 1)};
  if (key.kind == HotkeyKeyKind::named)
    return named_key(key.named);
  throw HotkeyExecutionError("system-hotkey-key-unsupported",
                             "Windows input injection does not support this key");
}

WORD physical_key(WORD key)
{
  switch (key)
  {
  case VK_CONTROL:
    return VK_LCONTROL;
  case VK_SHIFT:
    return VK_LSHIFT;
  case VK_MENU:
    return VK_LMENU;
  default:
    return key;
  }
}

INPUT keyboard_input(const NativeKey& key, bool key_up)
{
  INPUT input{};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = key.virtual_key;
  input.ki.dwFlags = key.flags | (key_up ? KEYEVENTF_KEYUP : 0);
  return input;
}
} // namespace

WindowsHotkeyExecutor::WindowsHotkeyExecutor()
    : WindowsHotkeyExecutor(
          [](std::span<INPUT> inputs)
          {
            return SendInput(
                static_cast<UINT>(inputs.size()), inputs.data(), static_cast<int>(sizeof(INPUT)));
          })
{
}

WindowsHotkeyExecutor::WindowsHotkeyExecutor(InputSender sender) : sender_(std::move(sender))
{
  if (!sender_)
    throw std::invalid_argument("Windows hotkey input sender is required");
}

bool WindowsHotkeyExecutor::available() const noexcept
{
  const auto desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (!desktop)
    return false;
  CloseDesktop(desktop);
  return true;
}

void WindowsHotkeyExecutor::trigger(std::span<const HotkeyKey> keys)
{
  if (keys.empty() || keys.size() > 8)
    throw std::invalid_argument("A hotkey chord requires between one and eight keys");
  std::vector<HotkeyKey> ordered(keys.begin(), keys.end());
  std::stable_partition(ordered.begin(), ordered.end(), is_hotkey_modifier);
  std::vector<NativeKey> native_keys;
  native_keys.reserve(ordered.size());
  for (const auto& key : ordered)
  {
    const auto mapped = native_key(key);
    if (std::ranges::any_of(
            native_keys,
            [&](const auto& existing)
            { return physical_key(existing.virtual_key) == physical_key(mapped.virtual_key); }))
      throw std::invalid_argument("A hotkey chord cannot repeat the same native key");
    native_keys.push_back(mapped);
  }

  std::vector<INPUT> inputs;
  inputs.reserve(native_keys.size() * 2);
  for (const auto& key : native_keys)
    inputs.push_back(keyboard_input(key, false));
  for (auto iterator = native_keys.rbegin(); iterator != native_keys.rend(); ++iterator)
    inputs.push_back(keyboard_input(*iterator, true));

  // Allocate cleanup before the first native mutation, including the partial-send failure path.
  std::vector<INPUT> releases;
  releases.reserve(native_keys.size());
  for (auto iterator = native_keys.rbegin(); iterator != native_keys.rend(); ++iterator)
    releases.push_back(keyboard_input(*iterator, true));
  const auto sent = sender_(inputs);
  if (sent == static_cast<UINT>(inputs.size()))
    return;
  if (sender_(releases) != static_cast<UINT>(releases.size()))
    throw std::runtime_error("Windows could not release the incomplete hotkey chord");
  throw std::runtime_error("Windows could not inject the complete hotkey chord");
}
} // namespace studio_remote::windows
