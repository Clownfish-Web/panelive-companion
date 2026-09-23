#include "windows_credential_store.hpp"
#include "windows_crypto.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace studio_remote::windows
{
namespace
{
constexpr std::size_t maximum_secret_bytes = 64 * 1024;
constexpr std::size_t maximum_encrypted_bytes = 128 * 1024;

[[noreturn]] void file_error(const char* operation)
{
  // Include the OS error, never the credential reference, path or secret value.
  throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}

void validate_key(std::string_view key)
{
  constexpr std::array<std::string_view, 4> reserved{"con", "prn", "aux", "nul"};
  const bool numbered_device = key.size() == 4 &&
                               (key.starts_with("com") || key.starts_with("lpt")) &&
                               key.back() >= '1' && key.back() <= '9';
  if (key.empty() || key.size() > 128 || numbered_device ||
      std::ranges::find(reserved, key) != reserved.end() ||
      !std::ranges::all_of(key,
                           [](char character)
                           {
                             return (character >= 'a' && character <= 'z') ||
                                    (character >= '0' && character <= '9') || character == '-' ||
                                    character == '_';
                           }))
    throw std::invalid_argument("Credential reference must be a lowercase file-safe identifier");
}

struct HandleCloser
{
  void operator()(void* value) const noexcept
  {
    if (!CloseHandle(value))
    {
      OutputDebugStringW(L"Could not close credential file handle.\n");
      std::terminate();
    }
  }
};
using File = std::unique_ptr<void, HandleCloser>;

struct PendingSecretFile
{
  std::filesystem::path path;
  File handle;
  bool committed = false;

  explicit PendingSecretFile(std::filesystem::path temporary) : path(std::move(temporary))
  {
    const auto opened = CreateFileW(path.c_str(),
                                    GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                                    nullptr);
    if (opened == INVALID_HANDLE_VALUE)
      file_error("Could not create credential checkpoint");
    handle.reset(opened);
  }

  ~PendingSecretFile()
  {
    handle.reset();
    if (!committed && !DeleteFileW(path.c_str()))
      OutputDebugStringW(L"Could not remove incomplete encrypted credential checkpoint.\n");
  }

  void replace(const std::filesystem::path& destination)
  {
    if (!FlushFileBuffers(handle.get()))
      file_error("Could not flush credential checkpoint");
    handle.reset();
    if (!MoveFileExW(
            path.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      file_error("Could not replace credential checkpoint");
    committed = true;
  }
};

void write_encrypted(HANDLE file, std::string_view value)
{
  while (!value.empty())
  {
    DWORD written = 0;
    if (!WriteFile(file, value.data(), static_cast<DWORD>(value.size()), &written, nullptr))
      file_error("Could not write credential checkpoint");
    if (written == 0 || written > value.size())
      throw std::runtime_error("Credential checkpoint write was incomplete");
    value.remove_prefix(written);
  }
}

std::string read_encrypted(HANDLE file)
{
  BY_HANDLE_FILE_INFORMATION information{};
  if (!GetFileInformationByHandle(file, &information))
    file_error("Could not inspect credential checkpoint");
  if (GetFileType(file) != FILE_TYPE_DISK ||
      (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
    throw std::runtime_error("Credential checkpoint is not a regular file");
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size))
    file_error("Could not size credential checkpoint");
  if (size.QuadPart <= 0 || size.QuadPart > static_cast<LONGLONG>(maximum_encrypted_bytes))
    throw std::runtime_error("Credential checkpoint has an invalid size");
  std::string value(static_cast<std::size_t>(size.QuadPart), '\0');
  std::size_t offset = 0;
  while (offset < value.size())
  {
    DWORD received = 0;
    const auto remaining = static_cast<DWORD>(value.size() - offset);
    if (!ReadFile(file, value.data() + offset, remaining, &received, nullptr))
      file_error("Could not read credential checkpoint");
    if (received == 0 || received > remaining)
      throw std::runtime_error("Credential checkpoint read was incomplete");
    offset += received;
  }
  const auto decoded = WindowsCrypto::base64url_decode(value);
  if (WindowsCrypto::base64url(decoded) != value)
    throw std::runtime_error("Credential checkpoint encoding is invalid");
  return value;
}
} // namespace

WindowsCredentialStore::WindowsCredentialStore(std::filesystem::path directory)
    : directory_(std::move(directory))
{
  std::filesystem::create_directories(directory_);
}
std::string WindowsCredentialStore::random_token(std::size_t bytes)
{
  return WindowsCrypto::base64url(WindowsCrypto::random_bytes(bytes));
}
std::string WindowsCredentialStore::hash(std::string_view value)
{
  return WindowsCrypto::sha256_hex(value);
}
bool WindowsCredentialStore::constant_time_equal(std::string_view a, std::string_view b)
{
  return WindowsCrypto::constant_time_equal(a, b);
}
std::string WindowsCredentialStore::uuid_v4()
{
  return WindowsCrypto::uuid_v4();
}
void WindowsCredentialStore::put(std::string_view key, std::string_view value)
{
  validate_key(key);
  if (value.size() > maximum_secret_bytes)
    throw std::invalid_argument("Credential exceeds the protected storage limit");
  const auto encrypted = WindowsCrypto::dpapi_protect(value);
  if (encrypted.empty() || encrypted.size() > maximum_encrypted_bytes)
    throw std::runtime_error("Encrypted credential exceeds the storage limit");
  // A unique file in the same directory keeps old contents intact until the flushed rename.
  PendingSecretFile pending(directory_ / (WindowsCrypto::uuid_v4() + ".pending-secret"));
  write_encrypted(pending.handle.get(), encrypted);
  pending.replace(directory_ / (std::string(key) + ".secret"));
}
std::string WindowsCredentialStore::get(std::string_view key)
{
  validate_key(key);
  const auto path = directory_ / (std::string(key) + ".secret");
  const auto opened = CreateFileW(path.c_str(),
                                  GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_DELETE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
                                  nullptr);
  if (opened == INVALID_HANDLE_VALUE)
  {
    if (GetLastError() == ERROR_FILE_NOT_FOUND)
      return {};
    file_error("Could not open credential checkpoint");
  }
  File file(opened);
  auto plain = WindowsCrypto::dpapi_unprotect(read_encrypted(file.get()));
  if (plain.size() > maximum_secret_bytes)
  {
    SecureZeroMemory(plain.data(), plain.size());
    throw std::runtime_error("Stored credential exceeds the protected storage limit");
  }
  return plain;
}
} // namespace studio_remote::windows
