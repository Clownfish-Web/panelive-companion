#include "windows_sign_in_startup.hpp"

#include <Windows.h>

#include <array>
#include <stdexcept>
#include <studio_remote/provider/provider_selection.hpp>
#include <system_error>
#include <vector>

namespace studio_remote::windows
{
namespace
{
constexpr wchar_t run_key_path[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t run_value_name[] = L"panelive-companion";

class RegistryKey
{
public:
  explicit RegistryKey(HKEY value = nullptr) : value_(value)
  {
  }

  ~RegistryKey()
  {
    if (value_)
      RegCloseKey(value_);
  }

  RegistryKey(const RegistryKey&) = delete;
  RegistryKey& operator=(const RegistryKey&) = delete;

  HKEY* receive()
  {
    return &value_;
  }

  HKEY get() const
  {
    return value_;
  }

private:
  HKEY value_ = nullptr;
};

std::filesystem::path current_executable()
{
  std::vector<wchar_t> value(32'768);
  const auto size = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
  if (size == 0 || size >= value.size())
    throw std::system_error(GetLastError(), std::system_category(), "Executable path unavailable");
  return std::filesystem::path(std::wstring(value.data(), size));
}

void require_success(LSTATUS status, std::string_view operation)
{
  if (status != ERROR_SUCCESS)
    throw std::system_error(
        static_cast<int>(status), std::system_category(), std::string(operation));
}
} // namespace

std::wstring build_sign_in_startup_command(const std::filesystem::path& executable,
                                           std::string_view provider_ids)
{
  if (executable.empty() || !executable.is_absolute())
    throw std::invalid_argument("Sign-in startup requires an absolute executable path");
  constexpr std::array built_providers{std::string_view{"obs"},
                                       std::string_view{"vmix"},
                                       std::string_view{"streamlabs"},
                                       std::string_view{"companion-teleprompter"}};
  const auto selection = parse_provider_selection(provider_ids, built_providers);
  const std::wstring providers(selection.normalized.begin(), selection.normalized.end());
  return L"\"" + executable.native() + L"\" --providers " + providers;
}

WindowsSignInStartup::WindowsSignInStartup(std::string provider_ids)
    : command_(build_sign_in_startup_command(current_executable(), provider_ids))
{
}

bool WindowsSignInStartup::enabled() const
{
  RegistryKey key;
  const auto opened =
      RegOpenKeyExW(HKEY_CURRENT_USER, run_key_path, 0, KEY_QUERY_VALUE, key.receive());
  if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND)
    return false;
  require_success(opened, "Could not open the current-user sign-in startup registry key");

  DWORD bytes = 0;
  const auto measured =
      RegGetValueW(key.get(), nullptr, run_value_name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
  if (measured == ERROR_FILE_NOT_FOUND)
    return false;
  require_success(measured, "Could not read the Companion sign-in startup registration");

  std::vector<wchar_t> value(bytes / sizeof(wchar_t));
  require_success(
      RegGetValueW(
          key.get(), nullptr, run_value_name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes),
      "Could not read the Companion sign-in startup registration");
  while (!value.empty() && value.back() == L'\0')
    value.pop_back();
  return std::wstring_view(value.data(), value.size()) == command_;
}

void WindowsSignInStartup::set_enabled(bool enabled) const
{
  RegistryKey key;
  if (!enabled)
  {
    const auto opened =
        RegOpenKeyExW(HKEY_CURRENT_USER, run_key_path, 0, KEY_SET_VALUE, key.receive());
    if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND)
      return;
    require_success(opened, "Could not open the current-user sign-in startup registry key");
    const auto removed = RegDeleteValueW(key.get(), run_value_name);
    if (removed != ERROR_FILE_NOT_FOUND)
      require_success(removed, "Could not disable Companion sign-in startup");
    return;
  }

  require_success(RegCreateKeyExW(HKEY_CURRENT_USER,
                                  run_key_path,
                                  0,
                                  nullptr,
                                  REG_OPTION_NON_VOLATILE,
                                  KEY_SET_VALUE,
                                  nullptr,
                                  key.receive(),
                                  nullptr),
                  "Could not open the current-user sign-in startup registry key");
  const auto bytes = static_cast<DWORD>((command_.size() + 1) * sizeof(wchar_t));
  require_success(RegSetValueExW(key.get(),
                                 run_value_name,
                                 0,
                                 REG_SZ,
                                 reinterpret_cast<const BYTE*>(command_.c_str()),
                                 bytes),
                  "Could not enable Companion sign-in startup");
}

} // namespace studio_remote::windows
