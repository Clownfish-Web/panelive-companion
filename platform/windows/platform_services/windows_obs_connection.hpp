#pragma once

#include <studio_remote/ui/management_status.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace studio_remote::windows
{
struct ObsConnectionSettings
{
  std::string host;
  std::uint16_t port = 4455;
  std::optional<std::string> password;
};

struct VmixConnectionStatus
{
  std::string host;
  std::uint16_t port = 8088;
  std::string username;
  bool connected = false;
  bool synchronized = false;
  std::string detail;
  std::uint8_t srt_program_output = 1;
  std::uint8_t srt_preview_output = 2;
  std::uint16_t srt_program_port = 10'000;
  std::uint16_t srt_preview_port = 10'001;
  std::uint32_t srt_latency_ms = 160;
};

struct VmixConnectionSettings
{
  std::string host;
  std::uint16_t port = 8088;
  std::string username;
  std::optional<std::string> password;
  std::uint8_t srt_program_output = 1;
  std::uint8_t srt_preview_output = 2;
  std::uint16_t srt_program_port = 10'000;
  std::uint16_t srt_preview_port = 10'001;
  std::uint32_t srt_latency_ms = 160;
};

inline bool valid_vmix_srt_settings(const VmixConnectionSettings& settings) noexcept
{
  return settings.srt_program_output >= 1 && settings.srt_program_output <= 4 &&
         settings.srt_preview_output >= 1 && settings.srt_preview_output <= 4 &&
         settings.srt_program_output != settings.srt_preview_output &&
         settings.srt_program_port != settings.srt_preview_port &&
         settings.srt_program_port != settings.port && settings.srt_preview_port != settings.port &&
         settings.srt_latency_ms >= 20 && settings.srt_latency_ms <= 8'000;
}

inline bool valid_obs_host(std::string_view host)
{
  if (host.empty() || host.size() > 253)
    return false;
  for (const unsigned char character : host)
    if (character <= 0x20 || character == 0x7f)
      return false;
  return true;
}

inline bool valid_production_host(std::string_view host)
{
  return valid_obs_host(host);
}

// ERROR_WINHTTP_CANNOT_CONNECT means the configured process is simply unavailable at its endpoint.
inline bool expected_production_backend_offline_error(std::uint32_t error) noexcept
{
  return error == 12'029;
}

} // namespace studio_remote::windows
