#include "windows_vmix_input_category_source.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>

namespace studio_remote::windows
{
namespace
{

constexpr std::uintmax_t maximum_preset_bytes = 16U << 20U;

std::filesystem::path current_preset_path()
{
  const auto length = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
  if (length == 0)
    return {};
  std::wstring roaming(length, L'\0');
  const auto written = GetEnvironmentVariableW(L"APPDATA", roaming.data(), length);
  if (written == 0 || written >= length)
    return {};
  roaming.resize(written);
  return std::filesystem::path(roaming) / "last.vmix";
}

} // namespace

WindowsVmixInputCategorySource::WindowsVmixInputCategorySource(bool available)
    : available_(available)
{
}

bool WindowsVmixInputCategorySource::available() const
{
  return available_.load();
}

std::optional<std::string> WindowsVmixInputCategorySource::read_current_preset()
{
  if (!available())
    return std::nullopt;
  std::lock_guard lock(mutex_);
  const auto path = current_preset_path();
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size == 0 || size > maximum_preset_bytes)
    return cached_preset_.empty() ? std::nullopt : std::optional(cached_preset_);
  const auto write_time = std::filesystem::last_write_time(path, error);
  if (error)
    return cached_preset_.empty() ? std::nullopt : std::optional(cached_preset_);
  if (!cached_preset_.empty() && size == cached_size_ && write_time == cached_write_time_)
    return cached_preset_;
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    return cached_preset_.empty() ? std::nullopt : std::optional(cached_preset_);
  std::string contents(static_cast<std::size_t>(size), '\0');
  if (!stream.read(contents.data(), static_cast<std::streamsize>(contents.size())))
    return cached_preset_.empty() ? std::nullopt : std::optional(cached_preset_);
  if (contents.find("</XML>") == std::string::npos)
    return cached_preset_.empty() ? std::nullopt : std::optional(cached_preset_);
  cached_preset_ = std::move(contents);
  cached_size_ = size;
  cached_write_time_ = write_time;
  return cached_preset_;
}

void WindowsVmixInputCategorySource::set_available(bool available)
{
  available_.store(available);
}

} // namespace studio_remote::windows
