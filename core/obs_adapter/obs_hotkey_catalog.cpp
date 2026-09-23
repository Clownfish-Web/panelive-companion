#include <studio_remote/obs/obs_hotkey_catalog.hpp>

#include <studio_remote/ipc/quick_transition_control_mapping.hpp>

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace studio_remote
{
namespace
{

std::optional<std::string> canonical_key_name(std::string_view identifier)
{
  constexpr std::string_view prefix = "OBS_KEY_";
  if (!identifier.starts_with(prefix))
    return std::nullopt;

  const auto suffix = identifier.substr(prefix.size());
  if (const auto parsed = parse_hotkey_key(suffix); parsed && !is_hotkey_modifier(*parsed))
    return hotkey_key_name(*parsed);

  static constexpr std::array aliases{
      std::pair{"RETURN", "ENTER"},
      std::pair{"ESCAPE", "ESC"},
      std::pair{"PAGEUP", "PAGE_UP"},
      std::pair{"PAGEDOWN", "PAGE_DOWN"},
      std::pair{"PRINT", "PRINT_SCREEN"},
      std::pair{"SCROLLLOCK", "SCROLL_LOCK"},
      std::pair{"CAPSLOCK", "CAPS_LOCK"},
      std::pair{"NUMLOCK", "NUM_LOCK"},
      std::pair{"MENU", "CONTEXT_MENU"},
      std::pair{"VOLUMEMUTE", "VOLUME_MUTE"},
      std::pair{"VOLUMEDOWN", "VOLUME_DOWN"},
      std::pair{"VOLUMEUP", "VOLUME_UP"},
      std::pair{"MEDIANEXT", "MEDIA_NEXT"},
      std::pair{"MEDIAPREVIOUS", "MEDIA_PREVIOUS"},
      std::pair{"MEDIASTOP", "MEDIA_STOP"},
      std::pair{"MEDIATOGGLEPLAYPAUSE", "MEDIA_PLAY_PAUSE"},
      std::pair{"NUMPLUS", "NUMPAD_ADD"},
      std::pair{"NUMMINUS", "NUMPAD_SUBTRACT"},
      std::pair{"NUMASTERISK", "NUMPAD_MULTIPLY"},
      std::pair{"NUMSLASH", "NUMPAD_DIVIDE"},
      std::pair{"NUMPERIOD", "NUMPAD_DECIMAL"},
      std::pair{"EQUAL", "EQUALS"},
      std::pair{"QUOTELEFT", "BACKQUOTE"},
      std::pair{"BRACKETLEFT", "LEFT_BRACKET"},
      std::pair{"BRACKETRIGHT", "RIGHT_BRACKET"},
      std::pair{"APOSTROPHE", "QUOTE"},
  };
  for (const auto& [obs_name, canonical_name] : aliases)
  {
    if (suffix == obs_name)
      return canonical_name;
  }

  if (suffix.size() == 4 && suffix.starts_with("NUM") && suffix.back() >= '0' &&
      suffix.back() <= '9')
    return "NUMPAD_" + std::string(1, suffix.back());
  return std::nullopt;
}

} // namespace

std::vector<ProductionHotkeyDefinition>
map_obs_hotkey_catalog(const std::vector<ObservedObsHotkeyBinding>& bindings,
                       ObsCommandKeySupport command_key)
{
  std::vector<ProductionHotkeyDefinition> result;
  result.reserve(bindings.size());
  for (const auto& binding : bindings)
  {
    if (binding.identifier.empty() || binding.label.empty())
      continue;

    ProductionHotkeyDefinition definition{binding.identifier, binding.label, {}};
    const auto key = canonical_key_name(binding.key_identifier);
    if (key)
    {
      if ((binding.modifiers & ipc::obs_hotkey_modifier_control) != 0)
        definition.keys.emplace_back("CTRL");
      if ((binding.modifiers & ipc::obs_hotkey_modifier_shift) != 0)
        definition.keys.emplace_back("SHIFT");
      if ((binding.modifiers & ipc::obs_hotkey_modifier_alt) != 0)
        definition.keys.emplace_back("ALT");
      if ((binding.modifiers & ipc::obs_hotkey_modifier_command) != 0)
      {
        if (command_key == ObsCommandKeySupport::unavailable)
        {
          definition.keys.clear();
          result.push_back(std::move(definition));
          continue;
        }
        definition.keys.emplace_back("CMD");
      }
      definition.keys.push_back(*key);
    }
    result.push_back(std::move(definition));
  }
  return result;
}

} // namespace studio_remote
