#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote
{
enum class HotkeyKeyKind
{
  letter,
  digit,
  function,
  named
};

enum class HotkeyNamedKey
{
  control,
  left_control,
  right_control,
  shift,
  left_shift,
  right_shift,
  alt,
  left_alt,
  right_alt,
  windows,
  left_windows,
  right_windows,
  escape,
  tab,
  enter,
  space,
  backspace,
  insert,
  delete_key,
  home,
  end,
  page_up,
  page_down,
  left,
  right,
  up,
  down,
  print_screen,
  scroll_lock,
  pause,
  caps_lock,
  num_lock,
  context_menu,
  numpad_0,
  numpad_1,
  numpad_2,
  numpad_3,
  numpad_4,
  numpad_5,
  numpad_6,
  numpad_7,
  numpad_8,
  numpad_9,
  numpad_add,
  numpad_subtract,
  numpad_multiply,
  numpad_divide,
  numpad_decimal,
  volume_mute,
  volume_down,
  volume_up,
  media_next,
  media_previous,
  media_stop,
  media_play_pause,
  browser_back,
  browser_forward,
  browser_refresh,
  browser_stop,
  browser_search,
  browser_favorites,
  browser_home,
  semicolon,
  equals,
  comma,
  minus,
  period,
  slash,
  backquote,
  left_bracket,
  backslash,
  right_bracket,
  quote,
  command
};

enum class HotkeyTarget
{
  production,
  system
};

struct HotkeyKey
{
  HotkeyKeyKind kind = HotkeyKeyKind::named;
  std::uint8_t value = 0;
  HotkeyNamedKey named = HotkeyNamedKey::escape;

  bool operator==(const HotkeyKey&) const = default;
};

struct ProductionHotkeyDefinition
{
  std::string id;
  std::string name;
  std::vector<std::string> keys;
  std::vector<std::vector<std::string>> bindings = {};
};

struct ProductionHotkeyTrigger
{
  std::string id;
  std::span<const HotkeyKey> keys;
};

std::optional<HotkeyKey> parse_hotkey_key(std::string_view value);
std::string hotkey_key_name(const HotkeyKey& key);
bool is_hotkey_modifier(const HotkeyKey& key) noexcept;

class HotkeyExecutionError : public std::runtime_error
{
public:
  HotkeyExecutionError(std::string reason, std::string message);

  const std::string& reason() const noexcept
  {
    return reason_;
  }

private:
  std::string reason_;
};

class IHotkeyExecutor
{
public:
  virtual ~IHotkeyExecutor() = default;
  virtual bool available() const noexcept = 0;
  virtual void trigger(std::span<const HotkeyKey> keys) = 0;
};
} // namespace studio_remote
