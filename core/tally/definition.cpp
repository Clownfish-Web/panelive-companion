#include <studio_remote/tally/definition.hpp>

#include <cstdint>
#include <set>

namespace studio_remote::tally
{
namespace
{
std::optional<std::uint32_t> next_scalar(std::string_view text, std::size_t& offset)
{
  const auto first = static_cast<unsigned char>(text[offset++]);
  if (first < 0x80)
  {
    return first;
  }
  const unsigned length = first >= 0xc2 && first <= 0xdf   ? 2u
                          : first >= 0xe0 && first <= 0xef ? 3u
                          : first >= 0xf0 && first <= 0xf4 ? 4u
                                                           : 0u;
  if (length == 0 || text.size() - offset < length - 1)
  {
    return std::nullopt;
  }
  std::uint32_t scalar = first & (0x7fu >> length);
  for (unsigned index = 1; index < length; ++index)
  {
    const auto byte = static_cast<unsigned char>(text[offset++]);
    if ((byte & 0xc0u) != 0x80u)
    {
      return std::nullopt;
    }
    scalar = (scalar << 6) | (byte & 0x3fu);
  }
  const std::uint32_t minimum = length == 2 ? 0x80u : length == 3 ? 0x800u : 0x10000u;
  if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
  {
    return std::nullopt;
  }
  return scalar;
}

bool whitespace(std::uint32_t scalar)
{
  return scalar == 0x20 || scalar == 0xa0 || scalar == 0x1680 ||
         (scalar >= 0x2000 && scalar <= 0x200a) || scalar == 0x2028 || scalar == 0x2029 ||
         scalar == 0x202f || scalar == 0x205f || scalar == 0x3000;
}

bool claims_live(std::string_view label)
{
  std::size_t offset = 0;
  std::string upper;
  while (offset < label.size())
  {
    const auto scalar = next_scalar(label, offset);
    if (!scalar || (*scalar > 0x7f && *scalar != 0x131))
    {
      return false;
    }
    // Dotless i is the only non-ASCII scalar whose uppercase can occur in LIVE or ON AIR.
    const auto value = *scalar == 0x131 ? 'I' : static_cast<char>(*scalar);
    upper.push_back(value >= 'a' && value <= 'z' ? static_cast<char>(value - 'a' + 'A') : value);
  }
  return upper == "LIVE" || upper == "ON AIR";
}

std::optional<ValidationError> validate_selections(const std::vector<Selection>& selections,
                                                   std::string_view field)
{
  if (selections.size() > 256)
  {
    return ValidationError{std::string(field), "selection-limit"};
  }
  std::set<std::string_view> ids;
  for (const auto& selection : selections)
  {
    if (!valid_text(selection.id, 128) || !valid_text(selection.name, 256))
    {
      return ValidationError{std::string(field), "invalid-selection"};
    }
    if (!ids.insert(selection.id).second)
    {
      return ValidationError{std::string(field), "duplicate-selection"};
    }
  }
  return std::nullopt;
}
} // namespace

bool valid_text(std::string_view text, std::size_t limit, bool allow_empty)
{
  std::size_t offset = 0;
  std::size_t count = 0;
  bool nonblank = false;
  while (offset < text.size())
  {
    const auto scalar = next_scalar(text, offset);
    if (!scalar || *scalar < 0x20 || (*scalar >= 0x7f && *scalar <= 0x9f) || ++count > limit)
    {
      return false;
    }
    nonblank = nonblank || !whitespace(*scalar);
  }
  return allow_empty || nonblank;
}

std::optional<ValidationError> validate(const Definition& definition)
{
  if (!valid_text(definition.id, 128))
  {
    return ValidationError{"id", "invalid-text"};
  }
  if (!valid_text(definition.label, 100))
  {
    return ValidationError{"label", "invalid-text"};
  }
  if (const auto error = validate_selections(definition.scenes, "scenes"))
  {
    return error;
  }
  if (const auto error = validate_selections(definition.sources, "sources"))
  {
    return error;
  }
  if (definition.binding == Binding::scene && definition.scenes.empty())
  {
    return ValidationError{"scenes", "selection-required"};
  }
  if (definition.binding == Binding::source)
  {
    if (definition.mode != Mode::flood && definition.mode != Mode::bar)
    {
      return ValidationError{"binding", "unsupported-mode-binding"};
    }
    if (definition.sources.empty())
    {
      return ValidationError{"sources", "selection-required"};
    }
  }
  for (const auto& [state, label] : definition.labels)
  {
    if (!valid_text(label, 100, true))
    {
      return ValidationError{"labels", "invalid-text"};
    }
    if (state == State::program && claims_live(label))
    {
      return ValidationError{"labels.PROGRAM", "program-is-not-live"};
    }
  }
  return std::nullopt;
}
} // namespace studio_remote::tally
