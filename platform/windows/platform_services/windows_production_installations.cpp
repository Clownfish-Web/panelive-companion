#include "windows_production_installations.hpp"

#include <windows.h>

#include <algorithm>
#include <iterator>
#include <string>

namespace studio_remote::windows
{
namespace
{

constexpr wchar_t companion_key_path[] = L"SOFTWARE\\panelive-companion";
constexpr wchar_t companion_vmix_value_name[] = L"VmixDirectory";
constexpr wchar_t companion_obs_value_name[] = L"ObsDirectory";
constexpr wchar_t uninstall_key_path[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
constexpr wchar_t display_name_value[] = L"DisplayName";
constexpr wchar_t install_location_value[] = L"InstallLocation";

// vMix publishes sibling products such as "vMix Social" and "vMix Video Codec" under the same
// uninstall root. Only the exact product name identifies the installation that carries the NDI
// runtime, so prefix matching must not be used here.
constexpr wchar_t vmix_display_name[] = L"vMix";

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

std::wstring environment_value(const wchar_t* name)
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

std::wstring key_string_value(HKEY key, const wchar_t* value_name)
{
  DWORD bytes = 0;
  if (RegGetValueW(key, nullptr, value_name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) !=
          ERROR_SUCCESS ||
      bytes < sizeof(wchar_t))
    return {};
  std::wstring text(bytes / sizeof(wchar_t), L'\0');
  if (RegGetValueW(key, nullptr, value_name, RRF_RT_REG_SZ, nullptr, text.data(), &bytes) !=
      ERROR_SUCCESS)
    return {};
  text.resize(bytes / sizeof(wchar_t));
  while (!text.empty() && text.back() == L'\0')
    text.pop_back();
  return text;
}

/// Adds a directory once, ignoring empty, duplicate, and no longer installed locations.
void append_installation_root(std::vector<std::filesystem::path>& roots, std::wstring value)
{
  if (value.empty())
    return;
  std::error_code error;
  auto candidate = std::filesystem::path(value).lexically_normal();
  if (!std::filesystem::is_directory(candidate, error))
    return;
  if (std::find(roots.begin(), roots.end(), candidate) != roots.end())
    return;
  roots.push_back(std::move(candidate));
}

/// Reads a Companion installation record; the current user takes precedence.
void append_companion_recorded_root(std::vector<std::filesystem::path>& roots, HKEY root, REGSAM view)
{
  RegistryKey key;
  if (RegOpenKeyExW(root, companion_key_path, 0, KEY_READ | view, key.receive()) !=
      ERROR_SUCCESS)
    return;
  append_installation_root(roots, key_string_value(key.get(), companion_vmix_value_name));
}

/// Reads the installation directory that the vMix installer itself recorded.
void append_uninstall_registered_roots(std::vector<std::filesystem::path>& roots,
                                       HKEY root,
                                       REGSAM view)
{
  RegistryKey uninstall;
  if (RegOpenKeyExW(root, uninstall_key_path, 0, KEY_READ | view, uninstall.receive()) !=
      ERROR_SUCCESS)
    return;

  for (DWORD index = 0;; ++index)
  {
    wchar_t subkey_name[256];
    DWORD subkey_length = static_cast<DWORD>(std::size(subkey_name));
    const auto status = RegEnumKeyExW(
        uninstall.get(), index, subkey_name, &subkey_length, nullptr, nullptr, nullptr, nullptr);
    if (status == ERROR_NO_MORE_ITEMS)
      return;
    if (status != ERROR_SUCCESS)
      continue;

    RegistryKey product;
    if (RegOpenKeyExW(uninstall.get(), subkey_name, 0, KEY_READ | view, product.receive()) !=
        ERROR_SUCCESS)
      continue;
    if (_wcsicmp(key_string_value(product.get(), display_name_value).c_str(), vmix_display_name) !=
        0)
      continue;
    append_installation_root(roots, key_string_value(product.get(), install_location_value));
  }
}

/// Published once by the composition root during startup and only read afterwards.
std::filesystem::path& configured_installation_root()
{
  static std::filesystem::path root;
  return root;
}

} // namespace

void set_configured_vmix_installation_root(std::filesystem::path configured_root)
{
  configured_installation_root() = std::move(configured_root);
}

std::filesystem::path vmix_ndi_runtime_directory(const std::filesystem::path& installation_root)
{
  if (installation_root.empty())
    return {};
  return (installation_root / L"ndi" / L"x64").lexically_normal();
}

std::vector<std::filesystem::path>
discover_vmix_installation_roots(const std::filesystem::path& configured_root)
{
  std::vector<std::filesystem::path> roots;

  // The operator's choice in the management window outranks every detected location.
  const auto& effective_configured =
      configured_root.empty() ? configured_installation_root() : configured_root;
  if (!effective_configured.empty())
    append_installation_root(roots, effective_configured.wstring());

  // An explicit record from the Companion installer wins next: it captures the location the
  // operator confirmed at install time, including a relocated or non default installation.
  for (const auto root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
    for (const auto view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY})
      append_companion_recorded_root(roots, root, view);

  // The vMix setup is a 32-bit installer, so its uninstall entry normally lives in the 32-bit
  // view. Both views and the per-user hive are read because none of that is guaranteed.
  append_uninstall_registered_roots(roots, HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);
  append_uninstall_registered_roots(roots, HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
  append_uninstall_registered_roots(roots, HKEY_CURRENT_USER, KEY_WOW64_32KEY);

  if (auto program_files_x86 = environment_value(L"ProgramFiles(x86)"); !program_files_x86.empty())
    append_installation_root(roots, program_files_x86 + L"\\vMix");
  return roots;
}

std::filesystem::path installed_obs_directory()
{
  for (const auto root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
    for (const auto view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY})
    {
      RegistryKey key;
      if (RegOpenKeyExW(root, companion_key_path, 0, KEY_READ | view, key.receive()) !=
          ERROR_SUCCESS)
        continue;
      if (auto value = key_string_value(key.get(), companion_obs_value_name); !value.empty())
        return std::filesystem::path(value).lexically_normal();
    }
  return {};
}

std::vector<std::filesystem::path>
discover_vmix_ndi_runtime_directories(const std::filesystem::path& configured_root)
{
  std::vector<std::filesystem::path> directories;
  for (const auto& root : discover_vmix_installation_roots(configured_root))
  {
    auto runtime_directory = vmix_ndi_runtime_directory(root);
    std::error_code error;
    if (std::filesystem::is_directory(runtime_directory, error))
      directories.push_back(std::move(runtime_directory));
  }
  return directories;
}

} // namespace studio_remote::windows
