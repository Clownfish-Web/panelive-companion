#include "windows_streamlabs_installations.hpp"

// windows.h must precede the trust headers.
// clang-format off
#include <windows.h>
#include <softpub.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <wintrust.h>
// clang-format on

#include <algorithm>
#include <array>
#include <cwctype>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <studio_remote/streamlabs/streamlabs_version.hpp>
#include <vector>

namespace studio_remote::windows
{
namespace
{

constexpr wchar_t companion_key_path[] = L"SOFTWARE\\panelive-companion";
constexpr wchar_t companion_value_name[] = L"StreamlabsDirectory";
constexpr wchar_t uninstall_key_path[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
constexpr wchar_t executable_name[] = L"Streamlabs OBS.exe";
constexpr wchar_t publisher_name[] = L"General Workings, Inc.";
constexpr wchar_t display_name_prefix[] = L"Streamlabs Desktop ";

class RegistryKey
{
public:
  ~RegistryKey()
  {
    if (value_)
      RegCloseKey(value_);
  }

  RegistryKey(const RegistryKey&) = delete;
  RegistryKey& operator=(const RegistryKey&) = delete;
  RegistryKey() = default;

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

class Handle
{
public:
  explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value)
  {
  }

  ~Handle()
  {
    if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr)
      CloseHandle(value_);
  }

  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;

  HANDLE get() const
  {
    return value_;
  }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

std::wstring registry_string(HKEY key, const wchar_t* name)
{
  DWORD bytes = 0;
  if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
      bytes < sizeof(wchar_t))
    return {};
  std::wstring result(bytes / sizeof(wchar_t), L'\0');
  if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, result.data(), &bytes) !=
      ERROR_SUCCESS)
    return {};
  result.resize(bytes / sizeof(wchar_t));
  while (!result.empty() && result.back() == L'\0')
    result.pop_back();
  return result;
}

std::string utf8(std::wstring_view value)
{
  if (value.empty())
    return {};
  const auto size = WideCharToMultiByte(CP_UTF8,
                                        WC_ERR_INVALID_CHARS,
                                        value.data(),
                                        static_cast<int>(value.size()),
                                        nullptr,
                                        0,
                                        nullptr,
                                        nullptr);
  if (size <= 0)
    return {};
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8,
                      WC_ERR_INVALID_CHARS,
                      value.data(),
                      static_cast<int>(value.size()),
                      result.data(),
                      size,
                      nullptr,
                      nullptr);
  return result;
}

std::wstring lower(std::wstring value)
{
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
  return value;
}

std::filesystem::path normalized_absolute(const std::filesystem::path& value)
{
  std::error_code error;
  auto absolute = std::filesystem::absolute(value, error);
  if (error)
    return {};
  auto canonical = std::filesystem::weakly_canonical(absolute, error);
  return error ? absolute.lexically_normal() : canonical;
}

std::filesystem::path directory_from_uninstall(std::wstring value)
{
  const auto marker = lower(value).find(L"uninstall streamlabs obs.exe");
  if (marker == std::wstring::npos)
    return {};
  auto executable = value.substr(0, marker + std::size(L"uninstall streamlabs obs.exe") - 1);
  if (!executable.empty() && executable.front() == L'"')
    executable.erase(executable.begin());
  if (!executable.empty() && executable.back() == L'"')
    executable.pop_back();
  return std::filesystem::path(executable).parent_path();
}

bool x64_image(const std::filesystem::path& path)
{
  Handle file(CreateFileW(path.c_str(),
                          GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          nullptr));
  if (file.get() == INVALID_HANDLE_VALUE)
    return false;
  IMAGE_DOS_HEADER dos{};
  DWORD read = 0;
  if (!ReadFile(file.get(), &dos, sizeof(dos), &read, nullptr) || read != sizeof(dos) ||
      dos.e_magic != IMAGE_DOS_SIGNATURE)
    return false;
  if (SetFilePointer(file.get(), dos.e_lfanew + sizeof(DWORD), nullptr, FILE_BEGIN) ==
      INVALID_SET_FILE_POINTER)
    return false;
  IMAGE_FILE_HEADER header{};
  return ReadFile(file.get(), &header, sizeof(header), &read, nullptr) && read == sizeof(header) &&
         header.Machine == IMAGE_FILE_MACHINE_AMD64;
}

std::wstring version_string_value(const std::filesystem::path& path, const wchar_t* name)
{
  DWORD ignored = 0;
  const auto size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
  if (size == 0)
    return {};
  std::vector<unsigned char> storage(size);
  if (!GetFileVersionInfoW(path.c_str(), 0, size, storage.data()))
    return {};
  struct LanguageAndCodePage
  {
    WORD language;
    WORD code_page;
  };
  LanguageAndCodePage* translations = nullptr;
  UINT translation_bytes = 0;
  if (!VerQueryValueW(storage.data(),
                      L"\\VarFileInfo\\Translation",
                      reinterpret_cast<void**>(&translations),
                      &translation_bytes) ||
      translation_bytes < sizeof(LanguageAndCodePage))
    return {};
  std::array<wchar_t, 128> query{};
  swprintf_s(query.data(),
             query.size(),
             L"\\StringFileInfo\\%04x%04x\\%s",
             translations[0].language,
             translations[0].code_page,
             name);
  wchar_t* value = nullptr;
  UINT characters = 0;
  if (!VerQueryValueW(
          storage.data(), query.data(), reinterpret_cast<void**>(&value), &characters) ||
      !value || characters == 0)
    return {};
  return std::wstring(value, characters - 1);
}

std::pair<bool, bool> signature_status(const std::filesystem::path& path)
{
  WINTRUST_FILE_INFO file_info{};
  file_info.cbStruct = sizeof(file_info);
  file_info.pcwszFilePath = path.c_str();
  WINTRUST_DATA trust_data{};
  trust_data.cbStruct = sizeof(trust_data);
  trust_data.dwUIChoice = WTD_UI_NONE;
  trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
  trust_data.dwUnionChoice = WTD_CHOICE_FILE;
  trust_data.pFile = &file_info;
  trust_data.dwStateAction = WTD_STATEACTION_VERIFY;
  trust_data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
  GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  const bool trusted = WinVerifyTrust(nullptr, &policy, &trust_data) == ERROR_SUCCESS;

  bool signer_matches = false;
  if (trusted && trust_data.hWVTStateData)
  {
    auto* provider = WTHelperProvDataFromStateData(trust_data.hWVTStateData);
    auto* signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
    auto* certificate = signer ? WTHelperGetProvCertFromChain(signer, 0) : nullptr;
    if (certificate && certificate->pCert)
    {
      std::array<wchar_t, 512> subject{};
      CertGetNameStringW(certificate->pCert,
                         CERT_NAME_SIMPLE_DISPLAY_TYPE,
                         0,
                         nullptr,
                         subject.data(),
                         static_cast<DWORD>(subject.size()));
      signer_matches = lower(subject.data()).find(L"logitech") != std::wstring::npos;
    }
  }
  trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(nullptr, &policy, &trust_data);
  return {trusted, signer_matches};
}

std::optional<StreamlabsInstallation> validate(std::filesystem::path directory)
{
  directory = normalized_absolute(directory);
  const auto executable = directory / executable_name;
  std::error_code error;
  if (directory.empty() || !std::filesystem::is_regular_file(executable, error))
    return std::nullopt;
  if (version_string_value(executable, L"ProductName") != L"Streamlabs Desktop" ||
      version_string_value(executable, L"CompanyName") != publisher_name)
    return std::nullopt;
  StreamlabsInstallation result;
  result.directory = std::move(directory);
  result.executable = executable;
  result.version = utf8(version_string_value(executable, L"FileVersion"));
  result.x64 = x64_image(executable);
  const auto [trusted, signer] = signature_status(executable);
  result.signature_trusted = trusted;
  result.signer_matches = signer;
  result.supported =
      result.x64 && trusted && signer && streamlabs::supported_desktop_version(result.version);
  result.running = streamlabs_process_matches_installation(result);
  return result;
}

void append_candidate(std::vector<std::filesystem::path>& candidates, std::filesystem::path path)
{
  path = normalized_absolute(path);
  if (path.empty())
    return;
  const auto key = lower(path.wstring());
  const auto duplicate =
      std::find_if(candidates.begin(),
                   candidates.end(),
                   [&](const auto& item) { return lower(item.wstring()) == key; });
  if (duplicate == candidates.end())
    candidates.push_back(std::move(path));
}

void append_companion_record(std::vector<std::filesystem::path>& candidates, HKEY root, REGSAM view)
{
  RegistryKey key;
  if (RegOpenKeyExW(root, companion_key_path, 0, KEY_READ | view, key.receive()) ==
      ERROR_SUCCESS)
    append_candidate(candidates, registry_string(key.get(), companion_value_name));
}

void append_uninstall_records(std::vector<std::filesystem::path>& candidates,
                              HKEY root,
                              REGSAM view)
{
  RegistryKey uninstall;
  if (RegOpenKeyExW(root, uninstall_key_path, 0, KEY_READ | view, uninstall.receive()) !=
      ERROR_SUCCESS)
    return;
  for (DWORD index = 0;; ++index)
  {
    std::array<wchar_t, 256> name{};
    DWORD length = static_cast<DWORD>(name.size());
    const auto status = RegEnumKeyExW(
        uninstall.get(), index, name.data(), &length, nullptr, nullptr, nullptr, nullptr);
    if (status == ERROR_NO_MORE_ITEMS)
      return;
    if (status != ERROR_SUCCESS)
      continue;
    RegistryKey product;
    if (RegOpenKeyExW(uninstall.get(), name.data(), 0, KEY_READ | view, product.receive()) !=
        ERROR_SUCCESS)
      continue;
    const auto display_name = registry_string(product.get(), L"DisplayName");
    const auto publisher = registry_string(product.get(), L"Publisher");
    if (!display_name.starts_with(display_name_prefix) || publisher != publisher_name)
      continue;
    auto location = registry_string(product.get(), L"InstallLocation");
    if (!location.empty())
      append_candidate(candidates, location);
    else
      append_candidate(
          candidates, directory_from_uninstall(registry_string(product.get(), L"UninstallString")));
  }
}

} // namespace

bool streamlabs_process_matches_installation(const StreamlabsInstallation& installation)
{
  Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (snapshot.get() == INVALID_HANDLE_VALUE)
    return false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot.get(), &entry))
    return false;
  const auto expected = lower(normalized_absolute(installation.executable).wstring());
  do
  {
    if (_wcsicmp(entry.szExeFile, executable_name) != 0)
      continue;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
    if (process.get() == INVALID_HANDLE_VALUE)
      continue;
    std::array<wchar_t, 32'768> path{};
    DWORD characters = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(process.get(), 0, path.data(), &characters) &&
        lower(normalized_absolute(std::wstring(path.data(), characters)).wstring()) == expected)
      return true;
  } while (Process32NextW(snapshot.get(), &entry));
  return false;
}

std::vector<StreamlabsInstallation> detect_streamlabs_installations()
{
  std::vector<std::filesystem::path> candidates;
  for (const auto root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
    for (const auto view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY})
      append_companion_record(candidates, root, view);
  append_uninstall_records(candidates, HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
  append_uninstall_records(candidates, HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);
  append_uninstall_records(candidates, HKEY_CURRENT_USER, KEY_WOW64_64KEY);
  append_uninstall_records(candidates, HKEY_CURRENT_USER, KEY_WOW64_32KEY);

  std::vector<StreamlabsInstallation> result;
  for (auto& candidate : candidates)
    if (auto installation = validate(std::move(candidate)))
      result.push_back(std::move(*installation));
  return result;
}

std::optional<StreamlabsInstallation> detect_supported_streamlabs_installation()
{
  auto installations = detect_streamlabs_installations();
  const auto found = std::find_if(installations.begin(),
                                  installations.end(),
                                  [](const StreamlabsInstallation& installation)
                                  { return installation.supported; });
  return found == installations.end() ? std::nullopt : std::optional(*found);
}

} // namespace studio_remote::windows
