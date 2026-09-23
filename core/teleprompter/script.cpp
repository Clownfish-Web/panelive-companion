#include <studio_remote/teleprompter/session.hpp>

#include <algorithm>
#include <charconv>
#include <utility>

namespace studio_remote::teleprompter
{
namespace
{
bool space(std::uint32_t value)
{
  return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == 0xa0 ||
         value == 0x1680 || (value >= 0x2000 && value <= 0x200a) || value == 0x2028 ||
         value == 0x2029 || value == 0x202f || value == 0x205f || value == 0x3000;
}

// Called only after valid_text has established complete, well-formed UTF-8.
std::uint32_t next_codepoint(std::string_view text, std::size_t& offset)
{
  const auto first = static_cast<unsigned char>(text[offset++]);
  if (first < 0x80)
    return first;
  const unsigned int count = first <= 0xdf ? 1 : first <= 0xef ? 2 : 3;
  std::uint32_t value = first & (0x7fU >> (count + 1));
  for (unsigned int index = 0; index < count; ++index)
    value = (value << 6) | (static_cast<unsigned char>(text[offset++]) & 0x3f);
  return value;
}

std::string_view trimmed(std::string_view value)
{
  auto begin = std::string_view::npos;
  std::size_t end = 0;
  std::size_t offset = 0;
  while (offset < value.size())
  {
    const auto previous = offset;
    if (!space(next_codepoint(value, offset)))
    {
      if (begin == std::string_view::npos)
        begin = previous;
      end = offset;
    }
  }
  return begin == std::string_view::npos ? std::string_view{} : value.substr(begin, end - begin);
}

bool valid_text(std::string_view text, bool multiline)
{
  for (std::size_t offset = 0; offset < text.size();)
  {
    const auto first = static_cast<unsigned char>(text[offset++]);
    if (first < 0x80)
    {
      if ((first < 0x20 && !(multiline && space(first))) || first == 0x7f)
        return false;
      continue;
    }
    const unsigned int count = first >= 0xc2 && first <= 0xdf   ? 1
                               : first >= 0xe0 && first <= 0xef ? 2
                               : first >= 0xf0 && first <= 0xf4 ? 3
                                                                : 0;
    if (count == 0 || text.size() - offset < count)
      return false;
    std::uint32_t codepoint = first & (0x7fU >> (count + 1));
    for (unsigned int index = 0; index < count; ++index)
    {
      const auto next = static_cast<unsigned char>(text[offset++]);
      if ((next & 0xc0) != 0x80)
        return false;
      codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((count == 1 && codepoint < 0x80) || (count == 2 && codepoint < 0x800) ||
        (count == 3 && codepoint < 0x10000) || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff) || (codepoint >= 0x80 && codepoint <= 0x9f))
      return false;
  }
  return true;
}

std::string normalize_newlines(std::string_view content)
{
  std::string result;
  result.reserve(content.size());
  for (std::size_t offset = 0; offset < content.size(); ++offset)
  {
    if (content[offset] == '\r')
    {
      result += '\n';
      if (offset + 1 < content.size() && content[offset + 1] == '\n')
        ++offset;
    }
    else
      result += content[offset];
  }
  return result;
}

std::uint64_t words(std::string_view content)
{
  std::uint64_t count = 0;
  bool in_word = false;
  std::size_t offset = 0;
  while (offset < content.size())
  {
    const bool current = !space(next_codepoint(content, offset));
    if (current && !in_word)
      ++count;
    in_word = current;
  }
  return count;
}

std::uint64_t reading_ms(std::uint64_t count, std::uint32_t speed)
{
  return (count * 60'000 + speed - 1) / speed;
}

bool parse_cue(std::string_view line, Cue& cue)
{
  if (!line.ends_with("]]"))
    return false;
  line = line.substr(2, line.size() - 4);
  if (line.starts_with("cue:"))
  {
    const auto label = trimmed(line.substr(4));
    if (label.empty() || label.size() > 80 || !valid_text(label, false))
      return false;
    cue.label = label;
    return true;
  }
  if (!line.starts_with("pause:"))
    return false;
  const auto duration = line.substr(6);
  const auto parsed =
      std::from_chars(duration.data(), duration.data() + duration.size(), cue.pause_ms);
  if (parsed.ec != std::errc{} || parsed.ptr != duration.data() + duration.size() ||
      cue.pause_ms > 60'000)
    return false;
  cue.type = CueType::pause;
  return true;
}

Result<CompiledScript> compile_lines(Draft draft)
{
  CompiledScript result;
  result.draft = std::move(draft);
  std::uint64_t pauses_ms = 0;
  std::size_t start = 0;
  std::size_t line_number = 0;
  const std::string_view content(result.draft.content);
  while (start < content.size())
  {
    ++line_number;
    const auto end = content.find('\n', start);
    const bool newline = end != std::string_view::npos;
    const auto line = content.substr(start, newline ? end - start : content.size() - start);
    const auto directive = trimmed(line);
    if (directive.starts_with("[["))
    {
      Cue cue;
      if (result.cues.size() >= 500 || !parse_cue(directive, cue))
        return {{},
                Error::invalid_script,
                "Invalid or excessive cue on line " + std::to_string(line_number)};
      cue.id = "cue-" + std::to_string(result.cues.size() + 1);
      cue.spoken_text_byte_offset = result.spoken_text.size();
      cue.estimated_at_ms =
          reading_ms(result.word_count, result.draft.words_per_minute) + pauses_ms;
      pauses_ms += cue.pause_ms;
      result.cues.push_back(std::move(cue));
    }
    else
    {
      std::string spoken_line(line);
      if (directive.starts_with("\\[["))
        spoken_line.erase(static_cast<std::size_t>(directive.data() - line.data()), 1);
      result.spoken_text += spoken_line;
      if (newline)
        result.spoken_text += '\n';
      result.word_count += words(spoken_line);
    }
    start = newline ? end + 1 : content.size();
  }
  if (result.word_count == 0)
    return {{}, Error::invalid_script, "Enter spoken text in the script content"};
  result.estimated_duration_ms =
      reading_ms(result.word_count, result.draft.words_per_minute) + pauses_ms;
  return {std::move(result), Error::none, {}};
}
} // namespace

std::vector<std::uint64_t> spoken_word_offsets(std::string_view valid_utf8)
{
  std::vector<std::uint64_t> result;
  bool in_word = false;
  std::size_t offset = 0;
  while (offset < valid_utf8.size())
  {
    const auto start = offset;
    const bool current = !space(next_codepoint(valid_utf8, offset));
    if (current && !in_word)
      result.push_back(start);
    in_word = current;
  }
  return result;
}

Result<CompiledScript> compile_script(Draft draft)
{
  if (!valid_text(draft.name, false) || trimmed(draft.name).empty() ||
      trimmed(draft.name).size() > 160)
    return {{},
            Error::invalid_script,
            "Enter a script name of 1-160 UTF-8 bytes without control characters"};
  if (draft.content.empty() || draft.content.size() > maximum_content_bytes ||
      !valid_text(draft.content, true))
    return {{}, Error::invalid_script, "Enter valid script text of at most 262144 UTF-8 bytes"};
  if (draft.words_per_minute < 40 || draft.words_per_minute > 400)
    return {{}, Error::invalid_script, "Reading speed must be between 40 and 400 words per minute"};
  draft.name = trimmed(draft.name);
  draft.content = normalize_newlines(draft.content);
  return compile_lines(std::move(draft));
}
} // namespace studio_remote::teleprompter
