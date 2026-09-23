#include "windows_vmix_output_geometry_source.hpp"

#include <Windows.h>

#include <fstream>
#include <string>

namespace studio_remote::windows
{
namespace
{

constexpr std::uintmax_t maximum_settings_bytes = 1U << 20U;
constexpr auto check_interval = std::chrono::milliseconds(250);

std::filesystem::path environment_path(const wchar_t* name)
{
  const auto length = GetEnvironmentVariableW(name, nullptr, 0);
  if (length == 0)
    return {};
  std::wstring value(length, L'\0');
  const auto written = GetEnvironmentVariableW(name, value.data(), length);
  if (written == 0 || written >= length)
    return {};
  value.resize(written);
  return value;
}

} // namespace

WindowsVmixOutputGeometrySource::WindowsVmixOutputGeometrySource(bool available)
    : available_(available)
{
}

std::optional<vmix::OutputGeometry> WindowsVmixOutputGeometrySource::current()
{
  if (!available_)
    return std::nullopt;
  std::lock_guard lock(mutex_);
  const auto now = std::chrono::steady_clock::now();
  if (now < next_check_)
    return cached_geometry_;
  next_check_ = now + check_interval;

  if (settings_path_.empty() || !std::filesystem::exists(settings_path_))
    settings_path_ = locate_settings();
  if (settings_path_.empty())
    return cached_geometry_;
  if (const auto geometry = read_settings(settings_path_))
    cached_geometry_ = geometry;
  return cached_geometry_;
}

std::filesystem::path WindowsVmixOutputGeometrySource::locate_settings() const
{
  std::filesystem::path newest;
  std::filesystem::file_time_type newest_time{};
  const auto local_app_data = environment_path(L"LOCALAPPDATA");
  std::error_code error;
  for (const auto* directory : {L"StudioCoast_Pty_Ltd", L"StudioCoast Pty Ltd"})
  {
    const auto settings_root = local_app_data / directory;
    error.clear();
    if (settings_root.empty() || !std::filesystem::is_directory(settings_root, error))
      continue;
    for (std::filesystem::recursive_directory_iterator entry(settings_root, error), end;
         !error && entry != end;
         entry.increment(error))
    {
      if (!entry->is_regular_file(error) || entry->path().filename() != "user.config")
        continue;
      const auto write_time = entry->last_write_time(error);
      if (!error && (newest.empty() || write_time > newest_time))
      {
        newest = entry->path();
        newest_time = write_time;
      }
    }
  }
  if (!newest.empty())
    return newest;

  const auto program_data = environment_path(L"PROGRAMDATA");
  const auto backup = program_data / "vMix" / "settingbackups" / "current.config";
  return std::filesystem::is_regular_file(backup, error) ? backup : std::filesystem::path{};
}

std::optional<vmix::OutputGeometry>
WindowsVmixOutputGeometrySource::read_settings(const std::filesystem::path& path)
{
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size == 0 || size > maximum_settings_bytes)
    return std::nullopt;
  const auto write_time = std::filesystem::last_write_time(path, error);
  if (error)
    return std::nullopt;
  if (cached_geometry_ && size == cached_size_ && write_time == cached_write_time_)
    return cached_geometry_;

  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    return std::nullopt;
  std::string contents(static_cast<std::size_t>(size), '\0');
  if (!stream.read(contents.data(), static_cast<std::streamsize>(contents.size())))
    return std::nullopt;
  const auto geometry = vmix::parse_output_geometry(contents);
  if (!geometry)
    return std::nullopt;
  cached_size_ = size;
  cached_write_time_ = write_time;
  return geometry;
}

} // namespace studio_remote::windows
