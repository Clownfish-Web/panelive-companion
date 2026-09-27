#include "linux_private_tls_storage.hpp"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <openssl/crypto.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace studio_remote::linux
{
namespace
{
void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

class Descriptor
{
public:
  explicit Descriptor(int value) : value_(value)
  {
    require(value >= 0, "Could not open private TLS storage");
  }
  ~Descriptor()
  {
    if (::close(value_) != 0)
      std::fputs("Could not close TLS storage descriptor\n", stderr);
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int get() const noexcept
  {
    return value_;
  }

private:
  int value_;
};

struct SecretBuffer
{
  std::string value;
  ~SecretBuffer()
  {
    OPENSSL_cleanse(value.data(), value.size());
  }
};

struct stat validate_object(int descriptor, bool directory)
{
  struct stat info
  {
  };
  require(fstat(descriptor, &info) == 0, "Could not inspect TLS storage file");
  require(directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode) && info.st_nlink == 1,
          "TLS storage must use ordinary directories and singly linked files");
  require(info.st_uid == geteuid(), "TLS storage must belong to the current user");
  require(fchmod(descriptor, directory ? 0700 : 0600) == 0,
          "Could not restrict TLS storage permissions");
  return info;
}

int open_directory(const std::filesystem::path& directory)
{
  if (!directory.parent_path().empty())
    std::filesystem::create_directories(directory.parent_path());
  require(mkdir(directory.c_str(), 0700) == 0 || errno == EEXIST,
          "Could not create private TLS directory");
  return ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}
} // namespace

struct PrivateTlsStorage::Impl
{
  explicit Impl(const std::filesystem::path& path) : directory(open_directory(path))
  {
    validate_object(directory.get(), true);
  }
  Descriptor directory;
};

PrivateTlsStorage::PrivateTlsStorage(const std::filesystem::path& directory)
    : impl_(std::make_unique<Impl>(directory))
{
}
PrivateTlsStorage::~PrivateTlsStorage() = default;

std::string PrivateTlsStorage::read(const std::filesystem::path& path) const
{
  const Descriptor file(openat(impl_->directory.get(),
                               path.filename().c_str(),
                               O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  const auto info = validate_object(file.get(), false);
  require(info.st_size > 0 && info.st_size <= 64 * 1024, "Stored TLS identity has an invalid size");
  SecretBuffer buffer{std::string(static_cast<std::size_t>(info.st_size), '\0')};
  std::size_t offset = 0;
  while (offset < buffer.value.size())
  {
    const auto count =
        ::read(file.get(), buffer.value.data() + offset, buffer.value.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    require(count > 0, "Could not read complete TLS identity");
    offset += static_cast<std::size_t>(count);
  }
  return std::move(buffer.value);
}

void PrivateTlsStorage::write_new(const std::filesystem::path& path, std::string_view bytes) const
{
  require(!bytes.empty() && bytes.size() <= 64 * 1024, "Invalid TLS identity write size");
  const Descriptor file(openat(impl_->directory.get(),
                               path.filename().c_str(),
                               O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                               0600));
  validate_object(file.get(), false);
  std::size_t offset = 0;
  while (offset < bytes.size())
  {
    const auto count = ::write(file.get(), bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    require(count > 0, "Could not persist complete TLS identity");
    offset += static_cast<std::size_t>(count);
  }
  require(fsync(file.get()) == 0 && fsync(impl_->directory.get()) == 0,
          "Could not flush TLS identity");
}
} // namespace studio_remote::linux
