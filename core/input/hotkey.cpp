#include <studio_remote/input/hotkey.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace studio_remote
{
namespace
{
using NamedEntry = std::pair<std::string_view, HotkeyNamedKey>;
constexpr std::array named_keys{
    NamedEntry{"CTRL", HotkeyNamedKey::control},
    NamedEntry{"CONTROL", HotkeyNamedKey::control},
    NamedEntry{"LEFT_CTRL", HotkeyNamedKey::left_control},
    NamedEntry{"RIGHT_CTRL", HotkeyNamedKey::right_control},
    NamedEntry{"SHIFT", HotkeyNamedKey::shift},
    NamedEntry{"LEFT_SHIFT", HotkeyNamedKey::left_shift},
    NamedEntry{"RIGHT_SHIFT", HotkeyNamedKey::right_shift},
    NamedEntry{"ALT", HotkeyNamedKey::alt},
    NamedEntry{"LEFT_ALT", HotkeyNamedKey::left_alt},
    NamedEntry{"RIGHT_ALT", HotkeyNamedKey::right_alt},
    NamedEntry{"CMD", HotkeyNamedKey::command},
    NamedEntry{"COMMAND", HotkeyNamedKey::command},
    NamedEntry{"WIN", HotkeyNamedKey::windows},
    NamedEntry{"WINDOWS", HotkeyNamedKey::windows},
    NamedEntry{"LEFT_WIN", HotkeyNamedKey::left_windows},
    NamedEntry{"RIGHT_WIN", HotkeyNamedKey::right_windows},
    NamedEntry{"ESC", HotkeyNamedKey::escape},
    NamedEntry{"ESCAPE", HotkeyNamedKey::escape},
    NamedEntry{"TAB", HotkeyNamedKey::tab},
    NamedEntry{"ENTER", HotkeyNamedKey::enter},
    NamedEntry{"SPACE", HotkeyNamedKey::space},
    NamedEntry{"BACKSPACE", HotkeyNamedKey::backspace},
    NamedEntry{"INSERT", HotkeyNamedKey::insert},
    NamedEntry{"DELETE", HotkeyNamedKey::delete_key},
    NamedEntry{"HOME", HotkeyNamedKey::home},
    NamedEntry{"END", HotkeyNamedKey::end},
    NamedEntry{"PAGE_UP", HotkeyNamedKey::page_up},
    NamedEntry{"PAGE_DOWN", HotkeyNamedKey::page_down},
    NamedEntry{"LEFT", HotkeyNamedKey::left},
    NamedEntry{"RIGHT", HotkeyNamedKey::right},
    NamedEntry{"UP", HotkeyNamedKey::up},
    NamedEntry{"DOWN", HotkeyNamedKey::down},
    NamedEntry{"PRINT_SCREEN", HotkeyNamedKey::print_screen},
    NamedEntry{"SCROLL_LOCK", HotkeyNamedKey::scroll_lock},
    NamedEntry{"PAUSE", HotkeyNamedKey::pause},
    NamedEntry{"CAPS_LOCK", HotkeyNamedKey::caps_lock},
    NamedEntry{"NUM_LOCK", HotkeyNamedKey::num_lock},
    NamedEntry{"CONTEXT_MENU", HotkeyNamedKey::context_menu},
    NamedEntry{"NUMPAD_0", HotkeyNamedKey::numpad_0},
    NamedEntry{"NUMPAD_1", HotkeyNamedKey::numpad_1},
    NamedEntry{"NUMPAD_2", HotkeyNamedKey::numpad_2},
    NamedEntry{"NUMPAD_3", HotkeyNamedKey::numpad_3},
    NamedEntry{"NUMPAD_4", HotkeyNamedKey::numpad_4},
    NamedEntry{"NUMPAD_5", HotkeyNamedKey::numpad_5},
    NamedEntry{"NUMPAD_6", HotkeyNamedKey::numpad_6},
    NamedEntry{"NUMPAD_7", HotkeyNamedKey::numpad_7},
    NamedEntry{"NUMPAD_8", HotkeyNamedKey::numpad_8},
    NamedEntry{"NUMPAD_9", HotkeyNamedKey::numpad_9},
    NamedEntry{"NUMPAD_ADD", HotkeyNamedKey::numpad_add},
    NamedEntry{"NUMPAD_SUBTRACT", HotkeyNamedKey::numpad_subtract},
    NamedEntry{"NUMPAD_MULTIPLY", HotkeyNamedKey::numpad_multiply},
    NamedEntry{"NUMPAD_DIVIDE", HotkeyNamedKey::numpad_divide},
    NamedEntry{"NUMPAD_DECIMAL", HotkeyNamedKey::numpad_decimal},
    NamedEntry{"VOLUME_MUTE", HotkeyNamedKey::volume_mute},
    NamedEntry{"VOLUME_DOWN", HotkeyNamedKey::volume_down},
    NamedEntry{"VOLUME_UP", HotkeyNamedKey::volume_up},
    NamedEntry{"MEDIA_NEXT", HotkeyNamedKey::media_next},
    NamedEntry{"MEDIA_PREVIOUS", HotkeyNamedKey::media_previous},
    NamedEntry{"MEDIA_STOP", HotkeyNamedKey::media_stop},
    NamedEntry{"MEDIA_PLAY_PAUSE", HotkeyNamedKey::media_play_pause},
    NamedEntry{"BROWSER_BACK", HotkeyNamedKey::browser_back},
    NamedEntry{"BROWSER_FORWARD", HotkeyNamedKey::browser_forward},
    NamedEntry{"BROWSER_REFRESH", HotkeyNamedKey::browser_refresh},
    NamedEntry{"BROWSER_STOP", HotkeyNamedKey::browser_stop},
    NamedEntry{"BROWSER_SEARCH", HotkeyNamedKey::browser_search},
    NamedEntry{"BROWSER_FAVORITES", HotkeyNamedKey::browser_favorites},
    NamedEntry{"BROWSER_HOME", HotkeyNamedKey::browser_home},
    NamedEntry{"SEMICOLON", HotkeyNamedKey::semicolon},
    NamedEntry{"EQUALS", HotkeyNamedKey::equals},
    NamedEntry{"COMMA", HotkeyNamedKey::comma},
    NamedEntry{"MINUS", HotkeyNamedKey::minus},
    NamedEntry{"PERIOD", HotkeyNamedKey::period},
    NamedEntry{"SLASH", HotkeyNamedKey::slash},
    NamedEntry{"BACKQUOTE", HotkeyNamedKey::backquote},
    NamedEntry{"LEFT_BRACKET", HotkeyNamedKey::left_bracket},
    NamedEntry{"BACKSLASH", HotkeyNamedKey::backslash},
    NamedEntry{"RIGHT_BRACKET", HotkeyNamedKey::right_bracket},
    NamedEntry{"QUOTE", HotkeyNamedKey::quote},
};

std::string uppercase(std::string_view value)
{
  std::string result(value);
  std::transform(result.begin(),
                 result.end(),
                 result.begin(),
                 [](unsigned char character)
                 { return static_cast<char>(std::toupper(character)); });
  return result;
}
} // namespace

std::optional<HotkeyKey> parse_hotkey_key(std::string_view value)
{
  const auto normalized = uppercase(value);
  if (normalized.size() == 1 && normalized.front() >= 'A' && normalized.front() <= 'Z')
    return HotkeyKey{HotkeyKeyKind::letter, static_cast<std::uint8_t>(normalized.front() - 'A')};
  if (normalized.size() == 1 && normalized.front() >= '0' && normalized.front() <= '9')
    return HotkeyKey{HotkeyKeyKind::digit, static_cast<std::uint8_t>(normalized.front() - '0')};
  if (normalized.size() >= 2 && normalized.front() == 'F')
  {
    unsigned value_number = 0;
    for (const auto character : normalized.substr(1))
    {
      if (character < '0' || character > '9')
        return std::nullopt;
      value_number = value_number * 10U + static_cast<unsigned>(character - '0');
      if (value_number > 24)
        return std::nullopt;
    }
    if (value_number >= 1 && value_number <= 24)
      return HotkeyKey{HotkeyKeyKind::function, static_cast<std::uint8_t>(value_number)};
  }
  const auto found = std::find_if(named_keys.begin(),
                                  named_keys.end(),
                                  [&](const auto& entry) { return entry.first == normalized; });
  if (found == named_keys.end())
    return std::nullopt;
  return HotkeyKey{HotkeyKeyKind::named, 0, found->second};
}

std::string hotkey_key_name(const HotkeyKey& key)
{
  if (key.kind == HotkeyKeyKind::letter)
    return std::string(1, static_cast<char>('A' + key.value));
  if (key.kind == HotkeyKeyKind::digit)
    return std::string(1, static_cast<char>('0' + key.value));
  if (key.kind == HotkeyKeyKind::function)
    return "F" + std::to_string(key.value);
  const auto found = std::find_if(named_keys.begin(),
                                  named_keys.end(),
                                  [&](const auto& entry) { return entry.second == key.named; });
  return found == named_keys.end() ? "UNKNOWN" : std::string(found->first);
}

bool is_hotkey_modifier(const HotkeyKey& key) noexcept
{
  if (key.kind != HotkeyKeyKind::named)
    return false;
  return key.named == HotkeyNamedKey::control || key.named == HotkeyNamedKey::left_control ||
         key.named == HotkeyNamedKey::right_control || key.named == HotkeyNamedKey::shift ||
         key.named == HotkeyNamedKey::left_shift || key.named == HotkeyNamedKey::right_shift ||
         key.named == HotkeyNamedKey::alt || key.named == HotkeyNamedKey::left_alt ||
         key.named == HotkeyNamedKey::right_alt || key.named == HotkeyNamedKey::windows ||
         key.named == HotkeyNamedKey::left_windows || key.named == HotkeyNamedKey::right_windows ||
         key.named == HotkeyNamedKey::command;
}

HotkeyExecutionError::HotkeyExecutionError(std::string reason, std::string message)
    : std::runtime_error(std::move(message)), reason_(std::move(reason))
{
}
} // namespace studio_remote
