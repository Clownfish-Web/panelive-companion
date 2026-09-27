#include "windows_private_tls_storage.hpp"

#include <Windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <stdexcept>
#include <utility>
#include <vector>

namespace studio_remote::windows
{
namespace
{
void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

struct HandleCloser
{
  void operator()(void* handle) const noexcept
  {
    if (!CloseHandle(handle))
      OutputDebugStringW(L"Could not close TLS storage handle\n");
  }
};
using Handle = std::unique_ptr<void, HandleCloser>;
using LocalMemory = std::unique_ptr<void, decltype(&LocalFree)>;

Handle checked_file(HANDLE raw)
{
  require(raw != INVALID_HANDLE_VALUE, "Could not open private TLS storage");
  return Handle(raw);
}

std::vector<BYTE> process_user()
{
  HANDLE raw = nullptr;
  require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw),
          "Could not query TLS storage owner");
  const Handle token(raw);
  DWORD size = 0;
  require(!GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size) &&
              GetLastError() == ERROR_INSUFFICIENT_BUFFER && size >= sizeof(TOKEN_USER),
          "Could not query TLS storage owner size");
  std::vector<BYTE> bytes(size);
  require(GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size),
          "Could not read TLS storage owner");
  return bytes;
}

LocalMemory private_descriptor(PSID user)
{
  wchar_t* raw_sid = nullptr;
  require(ConvertSidToStringSidW(user, &raw_sid), "Could not encode TLS storage owner");
  const LocalMemory sid(raw_sid, LocalFree);
  const std::wstring sddl =
      L"O:" + std::wstring(raw_sid) + L"D:P(A;;FA;;;SY)(A;;FA;;;" + raw_sid + L")";
  PSECURITY_DESCRIPTOR raw = nullptr;
  require(ConvertStringSecurityDescriptorToSecurityDescriptorW(
              sddl.c_str(), SDDL_REVISION_1, &raw, nullptr),
          "Could not construct private TLS permissions");
  return LocalMemory(raw, LocalFree);
}

void validate_object(HANDLE file, bool directory)
{
  BY_HANDLE_FILE_INFORMATION info{};
  require(GetFileType(file) == FILE_TYPE_DISK && GetFileInformationByHandle(file, &info),
          "Could not inspect TLS storage file");
  require((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
              ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory &&
              (directory || info.nNumberOfLinks == 1),
          "TLS storage must use ordinary directories and singly linked files");
}
} // namespace

struct PrivateTlsStorage::Impl
{
  std::vector<BYTE> user = process_user();
  LocalMemory descriptor = private_descriptor(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid);
  Handle directory;

  void restrict_permissions(HANDLE file) const
  {
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR raw = nullptr;
    const auto result = GetSecurityInfo(
        file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &raw);
    const LocalMemory previous(raw, LocalFree);
    const auto* token_user = reinterpret_cast<const TOKEN_USER*>(user.data());
    require(result == ERROR_SUCCESS && owner && IsValidSid(owner) &&
                EqualSid(owner, token_user->User.Sid),
            "TLS storage must belong to the current user");
    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    PACL dacl = nullptr;
    require(GetSecurityDescriptorDacl(descriptor.get(), &present, &dacl, &defaulted) && present &&
                dacl,
            "Could not read private TLS permissions");
    require(SetSecurityInfo(file,
                            SE_FILE_OBJECT,
                            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                            nullptr,
                            nullptr,
                            dacl,
                            nullptr) == ERROR_SUCCESS,
            "Could not restrict TLS storage permissions");
  }
};

PrivateTlsStorage::PrivateTlsStorage(const std::filesystem::path& directory)
    : impl_(std::make_unique<Impl>())
{
  if (!directory.parent_path().empty())
    std::filesystem::create_directories(directory.parent_path());
  SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), impl_->descriptor.get(), FALSE};
  require(CreateDirectoryW(directory.c_str(), &attributes) ||
              GetLastError() == ERROR_ALREADY_EXISTS,
          "Could not create private TLS directory");
  // SetSecurityInfo does not propagate to children for a MAXIMUM_ALLOWED handle. Otherwise
  // removing inherited directory grants can strip access from unrelated settings/database files.
  impl_->directory =
      checked_file(CreateFileW(directory.c_str(),
                               MAXIMUM_ALLOWED,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                               nullptr));
  validate_object(impl_->directory.get(), true);
  impl_->restrict_permissions(impl_->directory.get());
}

PrivateTlsStorage::~PrivateTlsStorage() = default;

std::string PrivateTlsStorage::read(const std::filesystem::path& path) const
{
  const auto file = checked_file(CreateFileW(path.c_str(),
                                             GENERIC_READ | READ_CONTROL | WRITE_DAC,
                                             FILE_SHARE_READ,
                                             nullptr,
                                             OPEN_EXISTING,
                                             FILE_FLAG_OPEN_REPARSE_POINT,
                                             nullptr));
  validate_object(file.get(), false);
  impl_->restrict_permissions(file.get());
  LARGE_INTEGER size{};
  require(GetFileSizeEx(file.get(), &size) && size.QuadPart > 0 && size.QuadPart <= 64 * 1024,
          "Stored TLS identity has an invalid size");
  struct Buffer
  {
    std::string value;
    ~Buffer()
    {
      SecureZeroMemory(value.data(), value.size());
    }
  } buffer{std::string(static_cast<std::size_t>(size.QuadPart), '\0')};
  DWORD read = 0;
  require(ReadFile(file.get(),
                   buffer.value.data(),
                   static_cast<DWORD>(buffer.value.size()),
                   &read,
                   nullptr) &&
              read == buffer.value.size(),
          "Could not read complete TLS identity");
  return std::move(buffer.value);
}

void PrivateTlsStorage::write_new(const std::filesystem::path& path, std::string_view bytes) const
{
  require(!bytes.empty() && bytes.size() <= 64 * 1024, "Invalid TLS identity write size");
  SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), impl_->descriptor.get(), FALSE};
  const auto file = checked_file(CreateFileW(
      path.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
  DWORD written = 0;
  require(
      WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
          written == bytes.size() && FlushFileBuffers(file.get()),
      "Could not persist complete TLS identity");
}
} // namespace studio_remote::windows
