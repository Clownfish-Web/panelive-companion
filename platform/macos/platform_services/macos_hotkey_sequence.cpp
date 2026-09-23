#include "macos_hotkey_sequence.hpp"

#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <array>
#include <utility>

namespace studio_remote::macos
{
namespace
{
struct NativeKey
{
  std::uint16_t code;
  std::uint64_t modifier = 0;
};
struct NamedKey
{
  HotkeyNamedKey key;
  NativeKey native;
};
constexpr NamedKey named_keys[]{
    {HotkeyNamedKey::control, {kVK_Control, kCGEventFlagMaskControl}},
    {HotkeyNamedKey::left_control, {kVK_Control, kCGEventFlagMaskControl}},
    {HotkeyNamedKey::right_control, {kVK_RightControl, kCGEventFlagMaskControl}},
    {HotkeyNamedKey::shift, {kVK_Shift, kCGEventFlagMaskShift}},
    {HotkeyNamedKey::left_shift, {kVK_Shift, kCGEventFlagMaskShift}},
    {HotkeyNamedKey::right_shift, {kVK_RightShift, kCGEventFlagMaskShift}},
    {HotkeyNamedKey::alt, {kVK_Option, kCGEventFlagMaskAlternate}},
    {HotkeyNamedKey::left_alt, {kVK_Option, kCGEventFlagMaskAlternate}},
    {HotkeyNamedKey::right_alt, {kVK_RightOption, kCGEventFlagMaskAlternate}},
    {HotkeyNamedKey::command, {kVK_Command, kCGEventFlagMaskCommand}},
    {HotkeyNamedKey::escape, {kVK_Escape}},
    {HotkeyNamedKey::tab, {kVK_Tab}},
    {HotkeyNamedKey::enter, {kVK_Return}},
    {HotkeyNamedKey::space, {kVK_Space}},
    {HotkeyNamedKey::backspace, {kVK_Delete}},
    {HotkeyNamedKey::delete_key, {kVK_ForwardDelete}},
    {HotkeyNamedKey::home, {kVK_Home}},
    {HotkeyNamedKey::end, {kVK_End}},
    {HotkeyNamedKey::page_up, {kVK_PageUp}},
    {HotkeyNamedKey::page_down, {kVK_PageDown}},
    {HotkeyNamedKey::left, {kVK_LeftArrow}},
    {HotkeyNamedKey::right, {kVK_RightArrow}},
    {HotkeyNamedKey::up, {kVK_UpArrow}},
    {HotkeyNamedKey::down, {kVK_DownArrow}},
    {HotkeyNamedKey::context_menu, {kVK_ContextualMenu}},
    {HotkeyNamedKey::numpad_0, {kVK_ANSI_Keypad0}},
    {HotkeyNamedKey::numpad_1, {kVK_ANSI_Keypad1}},
    {HotkeyNamedKey::numpad_2, {kVK_ANSI_Keypad2}},
    {HotkeyNamedKey::numpad_3, {kVK_ANSI_Keypad3}},
    {HotkeyNamedKey::numpad_4, {kVK_ANSI_Keypad4}},
    {HotkeyNamedKey::numpad_5, {kVK_ANSI_Keypad5}},
    {HotkeyNamedKey::numpad_6, {kVK_ANSI_Keypad6}},
    {HotkeyNamedKey::numpad_7, {kVK_ANSI_Keypad7}},
    {HotkeyNamedKey::numpad_8, {kVK_ANSI_Keypad8}},
    {HotkeyNamedKey::numpad_9, {kVK_ANSI_Keypad9}},
    {HotkeyNamedKey::numpad_add, {kVK_ANSI_KeypadPlus}},
    {HotkeyNamedKey::numpad_subtract, {kVK_ANSI_KeypadMinus}},
    {HotkeyNamedKey::numpad_multiply, {kVK_ANSI_KeypadMultiply}},
    {HotkeyNamedKey::numpad_divide, {kVK_ANSI_KeypadDivide}},
    {HotkeyNamedKey::numpad_decimal, {kVK_ANSI_KeypadDecimal}},
    {HotkeyNamedKey::volume_mute, {kVK_Mute}},
    {HotkeyNamedKey::volume_down, {kVK_VolumeDown}},
    {HotkeyNamedKey::volume_up, {kVK_VolumeUp}},
    {HotkeyNamedKey::semicolon, {kVK_ANSI_Semicolon}},
    {HotkeyNamedKey::equals, {kVK_ANSI_Equal}},
    {HotkeyNamedKey::comma, {kVK_ANSI_Comma}},
    {HotkeyNamedKey::minus, {kVK_ANSI_Minus}},
    {HotkeyNamedKey::period, {kVK_ANSI_Period}},
    {HotkeyNamedKey::slash, {kVK_ANSI_Slash}},
    {HotkeyNamedKey::backquote, {kVK_ANSI_Grave}},
    {HotkeyNamedKey::left_bracket, {kVK_ANSI_LeftBracket}},
    {HotkeyNamedKey::backslash, {kVK_ANSI_Backslash}},
    {HotkeyNamedKey::right_bracket, {kVK_ANSI_RightBracket}},
    {HotkeyNamedKey::quote, {kVK_ANSI_Quote}}};

NativeKey native_key(const HotkeyKey& key)
{
  constexpr std::array<std::uint16_t, 26> letters{
      kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E, kVK_ANSI_F, kVK_ANSI_G,
      kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J, kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N,
      kVK_ANSI_O, kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T, kVK_ANSI_U,
      kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y, kVK_ANSI_Z};
  constexpr std::array<std::uint16_t, 10> digits{kVK_ANSI_0,
                                                 kVK_ANSI_1,
                                                 kVK_ANSI_2,
                                                 kVK_ANSI_3,
                                                 kVK_ANSI_4,
                                                 kVK_ANSI_5,
                                                 kVK_ANSI_6,
                                                 kVK_ANSI_7,
                                                 kVK_ANSI_8,
                                                 kVK_ANSI_9};
  constexpr std::array<std::uint16_t, 20> functions{
      kVK_F1,  kVK_F2,  kVK_F3,  kVK_F4,  kVK_F5,  kVK_F6,  kVK_F7,  kVK_F8,  kVK_F9,  kVK_F10,
      kVK_F11, kVK_F12, kVK_F13, kVK_F14, kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20};
  if (key.kind == HotkeyKeyKind::letter && key.value < letters.size())
    return {letters[key.value]};
  if (key.kind == HotkeyKeyKind::digit && key.value < digits.size())
    return {digits[key.value]};
  if (key.kind == HotkeyKeyKind::function && key.value >= 1 && key.value <= functions.size())
    return {functions[key.value - 1]};
  if (key.kind == HotkeyKeyKind::named)
    for (const auto& entry : named_keys)
      if (entry.key == key.named)
        return entry.native;
  throw HotkeyExecutionError("system-hotkey-key-unsupported",
                             "macOS input injection does not support this key");
}

std::uint64_t modifier_flags(std::span<const NativeKey> keys, std::uint64_t initial)
{
  for (const auto& key : keys)
    initial |= key.modifier;
  return initial;
}
} // namespace

std::vector<NativeKeyboardEvent> keyboard_sequence(std::span<const HotkeyKey> keys,
                                                   std::uint64_t initial_flags)
{
  if (keys.empty() || keys.size() > 8)
    throw std::invalid_argument("A hotkey chord requires between one and eight keys");
  std::vector<HotkeyKey> ordered(keys.begin(), keys.end());
  std::stable_partition(ordered.begin(), ordered.end(), is_hotkey_modifier);
  std::vector<NativeKey> native;
  native.reserve(ordered.size());
  for (const auto& key : ordered)
  {
    const auto mapped = native_key(key);
    if (std::ranges::any_of(native, [&](const auto& value) { return value.code == mapped.code; }))
      throw std::invalid_argument("A hotkey chord cannot repeat the same native key");
    native.push_back(mapped);
  }
  std::vector<NativeKeyboardEvent> events;
  events.reserve(native.size() * 2);
  for (std::size_t count = 1; count <= native.size(); ++count)
    events.push_back({native[count - 1].code,
                      modifier_flags(std::span(native).first(count), initial_flags),
                      true});
  for (std::size_t count = native.size(); count > 0; --count)
    events.push_back({native[count - 1].code,
                      modifier_flags(std::span(native).first(count - 1), initial_flags),
                      false});
  return events;
}
} // namespace studio_remote::macos
