#include "windows_startup_options.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>

namespace studio_remote::windows
{
unsigned startup_integer(std::string_view value, unsigned minimum, unsigned maximum)
{
  if (value.empty())
    throw std::invalid_argument("Startup numeric option requires a value");
  unsigned result = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result < minimum ||
      result > maximum)
    throw std::invalid_argument("Startup numeric option is outside its supported range");
  return result;
}

std::vector<StartupOption> parse_startup_options(std::span<const std::string_view> arguments)
{
  constexpr std::array<std::string_view, 21> supported_options{"--providers",
                                                               "--set-sign-in-startup",
                                                               "--data-directory",
                                                               "--name",
                                                               "--obs-password",
                                                               "--obs-host",
                                                               "--obs-port",
                                                               "--vmix-host",
                                                               "--vmix-http-port",
                                                               "--vmix-username",
                                                               "--vmix-password",
                                                               "--vmix-program-ndi-source",
                                                               "--vmix-preview-ndi-source",
                                                               "--vmix-srt-program-output",
                                                               "--vmix-srt-preview-output",
                                                               "--vmix-srt-program-port",
                                                               "--vmix-srt-preview-port",
                                                               "--vmix-srt-latency-ms",
                                                               "--vmix-streaming-configured",
                                                               "--gateway-port",
                                                               "--open-management"};
  std::vector<StartupOption> options;
  for (std::size_t index = 0; index < arguments.size(); ++index)
  {
    const auto name = arguments[index];
    if (std::find(supported_options.begin(), supported_options.end(), name) ==
        supported_options.end())
      throw std::invalid_argument("Unknown startup option");
    if (name == "--open-management")
    {
      options.push_back({name, {}});
      continue;
    }
    if (++index == arguments.size() || arguments[index].starts_with("--"))
      throw std::invalid_argument("Startup option requires a value");
    const auto value = arguments[index];
    if (name.ends_with("-port"))
      startup_integer(value, 1, 65'535);
    else if (name.ends_with("-output"))
      startup_integer(value, 1, 4);
    else if (name == "--vmix-srt-latency-ms")
      startup_integer(value, 20, 8'000);
    else if (name == "--vmix-streaming-configured" && value != "true" && value != "false")
      throw std::invalid_argument("vMix streaming configured must be true or false");
    else if (name == "--set-sign-in-startup" && value != "enabled" && value != "disabled")
      throw std::invalid_argument("Sign-in startup must be enabled or disabled");
    options.push_back({name, value});
  }
  return options;
}
} // namespace studio_remote::windows
