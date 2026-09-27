#include <studio_remote/vmix/vmix_state.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <initializer_list>
#include <stdexcept>
#include <studio_remote/build/vmix_category_colors.hpp>
#include <studio_remote/common/utf8.hpp>
#include <unordered_map>

namespace studio_remote::vmix
{
namespace
{

using Attributes = std::unordered_map<std::string, std::string>;

bool xml_character(std::uint32_t value)
{
  return value == 0x9 || value == 0xa || value == 0xd || (value >= 0x20 && value <= 0xd7ff) ||
         (value >= 0xe000 && value <= 0xfffd) || (value >= 0x10000 && value <= 0x10ffff);
}

void validate_xml_characters(std::string_view text)
{
  // UTF-8 scalar validation rejects surrogates and out-of-range encodings. XML additionally
  // excludes most ASCII controls and the two BMP noncharacters, including in ignored fields.
  if (!valid_utf8(text) || text.find("\xef\xbf\xbe") != std::string_view::npos ||
      text.find("\xef\xbf\xbf") != std::string_view::npos ||
      std::any_of(text.begin(),
                  text.end(),
                  [](unsigned char byte) { return byte < 0x20 && !xml_character(byte); }))
    throw std::runtime_error("vMix XML contains invalid character data");
}

void append_utf8(std::string& output, std::uint32_t value)
{
  if (value <= 0x7f)
    output.push_back(static_cast<char>(value));
  else if (value <= 0x7ff)
  {
    output.push_back(static_cast<char>(0xc0 | (value >> 6)));
    output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
  }
  else if (value <= 0xffff)
  {
    output.push_back(static_cast<char>(0xe0 | (value >> 12)));
    output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
  }
  else if (value <= 0x10ffff)
  {
    output.push_back(static_cast<char>(0xf0 | (value >> 18)));
    output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
  }
}

std::string decode_xml(std::string_view value)
{
  std::string result;
  result.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index)
  {
    if (value[index] != '&')
    {
      result.push_back(value[index]);
      continue;
    }
    const auto end = value.find(';', index + 1);
    if (end == std::string_view::npos)
      throw std::runtime_error("vMix XML contains an unterminated entity");
    const auto entity = value.substr(index + 1, end - index - 1);
    if (entity == "amp")
      result.push_back('&');
    else if (entity == "lt")
      result.push_back('<');
    else if (entity == "gt")
      result.push_back('>');
    else if (entity == "quot")
      result.push_back('"');
    else if (entity == "apos")
      result.push_back('\'');
    else if (!entity.empty() && entity.front() == '#')
    {
      std::uint32_t code_point = 0;
      auto digits = entity.substr(1);
      int base = 10;
      if (!digits.empty() && digits.front() == 'x')
      {
        digits.remove_prefix(1);
        base = 16;
      }
      const auto parsed =
          std::from_chars(digits.data(), digits.data() + digits.size(), code_point, base);
      if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() ||
          !xml_character(code_point))
        throw std::runtime_error("vMix XML contains an invalid numeric entity");
      append_utf8(result, code_point);
    }
    else
      throw std::runtime_error("vMix XML contains an unsupported entity");
    index = end;
  }
  return result;
}

Attributes parse_attributes(std::string_view tag)
{
  Attributes result;
  auto position = tag.find_first_of(" \t\r\n");
  while (position != std::string_view::npos)
  {
    position = tag.find_first_not_of(" \t\r\n/", position);
    if (position == std::string_view::npos || tag[position] == '>')
      break;
    const auto equals = tag.find('=', position);
    if (equals == std::string_view::npos)
      break;
    auto name_end = equals;
    while (name_end > position && std::isspace(static_cast<unsigned char>(tag[name_end - 1])))
      --name_end;
    auto value_start = tag.find_first_not_of(" \t\r\n", equals + 1);
    if (value_start == std::string_view::npos ||
        (tag[value_start] != '"' && tag[value_start] != '\''))
      throw std::runtime_error("vMix XML contains an invalid attribute");
    const auto quote = tag[value_start++];
    const auto value_end = tag.find(quote, value_start);
    if (value_end == std::string_view::npos)
      throw std::runtime_error("vMix XML contains an unterminated attribute");
    if (!result
             .emplace(std::string(tag.substr(position, name_end - position)),
                      decode_xml(tag.substr(value_start, value_end - value_start)))
             .second)
      throw std::runtime_error("vMix XML contains duplicate attributes");
    position = value_end + 1;
  }
  return result;
}

std::vector<std::pair<Attributes, std::string_view>> elements(std::string_view xml,
                                                              std::string_view name)
{
  std::vector<std::pair<Attributes, std::string_view>> result;
  const auto opening = "<" + std::string(name);
  const auto closing = "</" + std::string(name) + ">";
  std::size_t position = 0;
  while ((position = xml.find(opening, position)) != std::string_view::npos)
  {
    const auto name_end = position + opening.size();
    if (name_end < xml.size() && !std::isspace(static_cast<unsigned char>(xml[name_end])) &&
        xml[name_end] != '>' && xml[name_end] != '/')
    {
      position = name_end;
      continue;
    }
    const auto tag_end = xml.find('>', name_end);
    if (tag_end == std::string_view::npos)
      throw std::runtime_error("vMix XML contains an unterminated element");
    const auto tag = xml.substr(position + 1, tag_end - position - 1);
    const bool self_closing = !tag.empty() &&
                              tag.find_last_not_of(" \t\r\n") != std::string_view::npos &&
                              tag[tag.find_last_not_of(" \t\r\n")] == '/';
    if (self_closing)
    {
      result.emplace_back(parse_attributes(tag), std::string_view{});
      position = tag_end + 1;
      continue;
    }
    const auto content_end = xml.find(closing, tag_end + 1);
    if (content_end == std::string_view::npos)
      throw std::runtime_error("vMix XML contains an unterminated " + std::string(name));
    result.emplace_back(parse_attributes(tag), xml.substr(tag_end + 1, content_end - tag_end - 1));
    position = content_end + closing.size();
  }
  return result;
}

std::string text(std::string_view xml, std::string_view name)
{
  const auto values = elements(xml, name);
  return values.empty() ? std::string{} : decode_xml(values.front().second);
}

std::uint32_t unsigned_value(std::string_view value, std::uint32_t fallback = 0)
{
  std::uint32_t result = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() ? result : fallback;
}

double decimal_value(std::string_view value, double fallback)
{
  try
  {
    std::size_t consumed = 0;
    const auto result = std::stod(std::string(value), &consumed);
    return consumed == value.size() ? result : fallback;
  }
  catch (const std::exception&)
  {
    return fallback;
  }
}

bool boolean_value(std::string value)
{
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](unsigned char character)
                 { return static_cast<char>(std::tolower(character)); });
  return value == "true" || value == "1";
}

std::string attribute(const Attributes& attributes, std::string_view name)
{
  const auto found = attributes.find(std::string(name));
  return found == attributes.end() ? std::string{} : found->second;
}

std::string lower_ascii(std::string value)
{
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](unsigned char character)
                 { return static_cast<char>(std::tolower(character)); });
  return value;
}

std::string first_attribute(const Attributes& attributes, std::initializer_list<const char*> names)
{
  for (const auto* name : names)
  {
    const auto value = attribute(attributes, name);
    if (!value.empty())
      return value;
  }
  return {};
}

std::string host_from_endpoint(std::string value)
{
  if (value.empty())
    return {};
  const auto scheme = value.find("://");
  if (scheme != std::string::npos)
    value.erase(0, scheme + 3);
  if (const auto credentials = value.find('@'); credentials != std::string::npos)
    value.erase(0, credentials + 1);
  if (const auto path = value.find('/'); path != std::string::npos)
    value.erase(path);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.pop_back();
  return value.empty() ? std::string{} : value.substr(0, 256);
}

std::string service_kind_from_host(std::string_view host)
{
  const auto value = lower_ascii(std::string(host));
  if (value.find("youtube") != std::string::npos || value.find("youtu.be") != std::string::npos)
    return "youtube";
  if (value.find("twitch") != std::string::npos)
    return "twitch";
  if (value.find("facebook") != std::string::npos || value.find("fbcdn") != std::string::npos)
    return "facebook";
  if (value.find("kick") != std::string::npos)
    return "kick";
  return value.empty() ? "other" : "custom";
}

} // namespace

State parse_state(std::string_view xml)
{
  validate_xml_characters(xml);
  if (xml.find("<vmix") == std::string_view::npos)
    throw std::runtime_error("vMix API did not return a vmix XML document");

  State result;
  result.version = text(xml, "version");
  result.active_input = unsigned_value(text(xml, "active"));
  result.preview_input = unsigned_value(text(xml, "preview"));
  result.recording = boolean_value(text(xml, "recording"));
  result.streaming = boolean_value(text(xml, "streaming"));

  for (const auto& [attributes, ignored] : elements(xml, "stream"))
  {
    static_cast<void>(ignored);
    StreamingDestination destination;
    destination.stream =
        unsigned_value(first_attribute(attributes, {"stream", "number", "index"}), 0);
    destination.name_or_key =
        first_attribute(attributes, {"nameOrKey", "name", "key", "label", "title"});
    destination.host =
        host_from_endpoint(first_attribute(attributes, {"host", "server", "url", "endpoint"}));
    destination.service_kind = service_kind_from_host(destination.host);
    destination.enabled = boolean_value(first_attribute(attributes, {"enabled", "selected"}));
    destination.configured =
        destination.enabled || !destination.name_or_key.empty() || !destination.host.empty();
    destination.active = boolean_value(first_attribute(attributes, {"active", "streaming"}));
    if (destination.configured)
      result.streaming_destinations.push_back(std::move(destination));
  }
  result.stream_configuration_known = !elements(xml, "streams").empty();

  const auto masters = elements(xml, "master");
  if (!masters.empty())
    result.master_volume = decimal_value(attribute(masters.front().first, "volume"), 100.0);

  const auto overlay_containers = elements(xml, "overlays");
  if (!overlay_containers.empty())
  {
    for (const auto& [attributes, content] : elements(overlay_containers.front().second, "overlay"))
    {
      static_cast<void>(attributes);
      const auto input_number = unsigned_value(decode_xml(content));
      if (input_number != 0)
        result.overlay_inputs.push_back(input_number);
    }
  }

  for (const auto& [attributes, content] : elements(xml, "input"))
  {
    Input input;
    input.key = attribute(attributes, "key");
    input.number = unsigned_value(attribute(attributes, "number"));
    input.type = attribute(attributes, "type");
    // vMix replaces offline devices with Placeholder inputs. Playback state is not availability:
    // valid still images and stopped media can be Paused, and a running stream can lack a signal.
    if (input.type == "Placeholder")
      continue;
    input.title = attribute(attributes, "title");
    if (input.title.empty())
      input.title = decode_xml(content.substr(0, content.find('<')));
    input.audio_capable =
        attributes.contains("muted") || attributes.contains("volume") || input.type == "Audio";
    input.muted = boolean_value(attribute(attributes, "muted"));
    input.solo = boolean_value(attribute(attributes, "solo"));
    input.balance = std::clamp(decimal_value(attribute(attributes, "balance"), 0.0), -1.0, 1.0);
    input.volume = decimal_value(attribute(attributes, "volume"), 100.0);
    input.meter_left = std::clamp(decimal_value(attribute(attributes, "meterF1"), 0.0), 0.0, 1.0);
    input.meter_right = std::clamp(decimal_value(attribute(attributes, "meterF2"), 0.0), 0.0, 1.0);
    input.audio_buses = attribute(attributes, "audiobusses");
    for (const auto& [layer_attributes, ignored] : elements(content, "overlay"))
    {
      static_cast<void>(ignored);
      const auto key = attribute(layer_attributes, "key");
      if (!key.empty())
        input.layers.push_back({unsigned_value(attribute(layer_attributes, "index")) + 1, key});
    }
    if (!input.key.empty() && input.number != 0)
      result.inputs.push_back(std::move(input));
  }

  for (const auto& [attributes, ignored] : elements(xml, "transition"))
  {
    static_cast<void>(ignored);
    const auto effect = attribute(attributes, "effect");
    const auto number = unsigned_value(attribute(attributes, "number"));
    if (!effect.empty() && number != 0)
      result.transitions.push_back(
          {number, effect, unsigned_value(attribute(attributes, "duration"), 1'000)});
  }
  std::sort(result.transitions.begin(),
            result.transitions.end(),
            [](const Transition& left, const Transition& right)
            { return left.number < right.number; });
  return result;
}

void apply_local_preset_metadata(State& state, std::string_view preset_xml)
{
  validate_xml_characters(preset_xml);
  // Category is an input property. Zero means that no color is assigned.
  std::unordered_map<std::string, std::string> colors_by_key;
  for (const auto& [attributes, ignored] : elements(preset_xml, "Input"))
  {
    static_cast<void>(ignored);
    const auto key = attribute(attributes, "Key");
    const auto category =
        unsigned_value(attribute(attributes, "Category"),
                       static_cast<std::uint32_t>(build::input_category_colors.size()));
    if (!key.empty() && category < build::input_category_colors.size() &&
        !build::input_category_colors[category].empty())
      colors_by_key.insert_or_assign(key, build::input_category_colors[category]);

    const auto input = std::find_if(state.inputs.begin(),
                                    state.inputs.end(),
                                    [&](const Input& candidate) { return candidate.key == key; });
    if (key.empty() || input == state.inputs.end())
      continue;
    const auto matrix_positions = elements(attribute(attributes, "Positions"), "MatrixPosition");
    for (auto& layer : input->layers)
    {
      const auto overlay_key = attribute(attributes, "Overlay" + std::to_string(layer.index - 1));
      if (overlay_key != layer.input_key || layer.index >= matrix_positions.size())
        continue;
      layer.visible = !boolean_value(text(matrix_positions[layer.index].second, "Hidden"));
      layer.visibility_known = true;
    }
  }
  for (auto& input : state.inputs)
    if (const auto color = colors_by_key.find(input.key); color != colors_by_key.end())
      input.color = color->second;
}

const Input* find_input(const State& state, std::string_view key_or_title)
{
  const auto found = std::find_if(
      state.inputs.begin(),
      state.inputs.end(),
      [&](const Input& input) { return input.key == key_or_title || input.title == key_or_title; });
  return found == state.inputs.end() ? nullptr : &*found;
}

const Input* find_input(const State& state, std::uint32_t number)
{
  const auto found = std::find_if(state.inputs.begin(),
                                  state.inputs.end(),
                                  [&](const Input& input) { return input.number == number; });
  return found == state.inputs.end() ? nullptr : &*found;
}

} // namespace studio_remote::vmix
