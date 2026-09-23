#pragma once

#include <span>
#include <string_view>
#include <vector>

namespace studio_remote::windows
{
struct StartupOption
{
  std::string_view name;
  std::string_view value;
};

// Views borrow argv storage. Validate every option before startup has side effects.
std::vector<StartupOption> parse_startup_options(std::span<const std::string_view> arguments);
unsigned startup_integer(std::string_view value, unsigned minimum, unsigned maximum);
} // namespace studio_remote::windows
