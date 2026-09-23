#include <studio_remote/obs/obs_hotkey_sequence.hpp>

#include <array>
#include <optional>
#include <utility>

namespace studio_remote
{
namespace
{
std::string obs_hotkey_key_id(const HotkeyKey& key)
{
  if (key.kind == HotkeyKeyKind::letter || key.kind == HotkeyKeyKind::digit ||
      key.kind == HotkeyKeyKind::function)
    return "OBS_KEY_" + hotkey_key_name(key);

  static constexpr std::array named_keys{
      std::pair{HotkeyNamedKey::escape, "OBS_KEY_ESCAPE"},
      std::pair{HotkeyNamedKey::tab, "OBS_KEY_TAB"},
      std::pair{HotkeyNamedKey::enter, "OBS_KEY_RETURN"},
      std::pair{HotkeyNamedKey::space, "OBS_KEY_SPACE"},
      std::pair{HotkeyNamedKey::backspace, "OBS_KEY_BACKSPACE"},
      std::pair{HotkeyNamedKey::insert, "OBS_KEY_INSERT"},
      std::pair{HotkeyNamedKey::delete_key, "OBS_KEY_DELETE"},
      std::pair{HotkeyNamedKey::home, "OBS_KEY_HOME"},
      std::pair{HotkeyNamedKey::end, "OBS_KEY_END"},
      std::pair{HotkeyNamedKey::page_up, "OBS_KEY_PAGEUP"},
      std::pair{HotkeyNamedKey::page_down, "OBS_KEY_PAGEDOWN"},
      std::pair{HotkeyNamedKey::left, "OBS_KEY_LEFT"},
      std::pair{HotkeyNamedKey::right, "OBS_KEY_RIGHT"},
      std::pair{HotkeyNamedKey::up, "OBS_KEY_UP"},
      std::pair{HotkeyNamedKey::down, "OBS_KEY_DOWN"},
      std::pair{HotkeyNamedKey::print_screen, "OBS_KEY_PRINT"},
      std::pair{HotkeyNamedKey::scroll_lock, "OBS_KEY_SCROLLLOCK"},
      std::pair{HotkeyNamedKey::pause, "OBS_KEY_PAUSE"},
      std::pair{HotkeyNamedKey::caps_lock, "OBS_KEY_CAPSLOCK"},
      std::pair{HotkeyNamedKey::num_lock, "OBS_KEY_NUMLOCK"},
      std::pair{HotkeyNamedKey::context_menu, "OBS_KEY_MENU"},
      std::pair{HotkeyNamedKey::volume_mute, "OBS_KEY_VOLUMEMUTE"},
      std::pair{HotkeyNamedKey::volume_down, "OBS_KEY_VOLUMEDOWN"},
      std::pair{HotkeyNamedKey::volume_up, "OBS_KEY_VOLUMEUP"},
      std::pair{HotkeyNamedKey::media_next, "OBS_KEY_MEDIANEXT"},
      std::pair{HotkeyNamedKey::media_previous, "OBS_KEY_MEDIAPREVIOUS"},
      std::pair{HotkeyNamedKey::media_stop, "OBS_KEY_MEDIASTOP"},
      std::pair{HotkeyNamedKey::media_play_pause, "OBS_KEY_MEDIATOGGLEPLAYPAUSE"},
      std::pair{HotkeyNamedKey::semicolon, "OBS_KEY_SEMICOLON"},
      std::pair{HotkeyNamedKey::equals, "OBS_KEY_EQUAL"},
      std::pair{HotkeyNamedKey::comma, "OBS_KEY_COMMA"},
      std::pair{HotkeyNamedKey::minus, "OBS_KEY_MINUS"},
      std::pair{HotkeyNamedKey::period, "OBS_KEY_PERIOD"},
      std::pair{HotkeyNamedKey::slash, "OBS_KEY_SLASH"},
      std::pair{HotkeyNamedKey::backquote, "OBS_KEY_QUOTELEFT"},
      std::pair{HotkeyNamedKey::left_bracket, "OBS_KEY_BRACKETLEFT"},
      std::pair{HotkeyNamedKey::backslash, "OBS_KEY_BACKSLASH"},
      std::pair{HotkeyNamedKey::right_bracket, "OBS_KEY_BRACKETRIGHT"},
      std::pair{HotkeyNamedKey::quote, "OBS_KEY_APOSTROPHE"},
  };
  for (const auto& [named, identifier] : named_keys)
  {
    if (key.named == named)
      return identifier;
  }
  throw HotkeyExecutionError("production-hotkey-key-unsupported",
                             "OBS does not support this key in a key-sequence request");
}

} // namespace

ObsHotkeySequence make_obs_hotkey_sequence(std::span<const HotkeyKey> keys,
                                           ObsCommandKeySupport command_key)
{
  ObsHotkeySequence sequence;
  std::optional<std::string> key_id;
  for (const auto& key : keys)
  {
    if (key.kind == HotkeyKeyKind::named)
    {
      switch (key.named)
      {
      case HotkeyNamedKey::control:
      case HotkeyNamedKey::left_control:
      case HotkeyNamedKey::right_control:
        sequence.control = true;
        continue;
      case HotkeyNamedKey::shift:
      case HotkeyNamedKey::left_shift:
      case HotkeyNamedKey::right_shift:
        sequence.shift = true;
        continue;
      case HotkeyNamedKey::alt:
      case HotkeyNamedKey::left_alt:
      case HotkeyNamedKey::right_alt:
        sequence.alt = true;
        continue;
      case HotkeyNamedKey::command:
        if (command_key == ObsCommandKeySupport::unavailable)
          throw HotkeyExecutionError("production-hotkey-modifier-unsupported",
                                     "This OBS adapter does not support the Command key");
        sequence.command = true;
        continue;
      case HotkeyNamedKey::windows:
      case HotkeyNamedKey::left_windows:
      case HotkeyNamedKey::right_windows:
        throw HotkeyExecutionError("production-hotkey-modifier-unsupported",
                                   "OBS key-sequence requests do not support the Windows key");
      default:
        break;
      }
    }
    if (key_id)
      throw HotkeyExecutionError("production-hotkey-chord-unsupported",
                                 "OBS accepts one key with optional modifiers");
    key_id = obs_hotkey_key_id(key);
  }
  if (!key_id)
    throw HotkeyExecutionError("production-hotkey-key-required",
                               "OBS requires one non-modifier key");
  sequence.key_id = std::move(*key_id);
  return sequence;
}
} // namespace studio_remote
