#include "macos_config_file.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <syslog.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace studio_remote::macos
{
namespace
{
void fail(const char* message)
{
  throw std::system_error(errno, std::generic_category(), message);
}

class Descriptor
{
public:
  explicit Descriptor(int value) : value_(value)
  {
    if (value_ < 0)
      fail("Could not open macOS configuration storage");
  }
  ~Descriptor()
  {
    if (value_ >= 0 && ::close(value_) != 0)
      ::syslog(LOG_ERR, "Could not close macOS configuration descriptor");
  }
  Descriptor(Descriptor&& other) noexcept : value_(std::exchange(other.value_, -1))
  {
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

Descriptor open_directory(const std::filesystem::path& directory)
{
  std::filesystem::create_directories(directory);
  Descriptor result(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  struct stat status{};
  if (::fstat(result.get(), &status) != 0)
    fail("Could not inspect configuration directory");
  if (status.st_uid != ::geteuid())
    throw std::runtime_error("Configuration directory belongs to another user");
  if (::fchmod(result.get(), 0700) != 0)
    fail("Could not restrict configuration directory");
  return result;
}

std::string temporary_name()
{
  std::array<unsigned char, 16> bytes{};
  ::arc4random_buf(bytes.data(), bytes.size());
  std::string result = "config.json.tmp-";
  constexpr char hex[] = "0123456789abcdef";
  for (const auto value : bytes)
  {
    result += hex[value >> 4];
    result += hex[value & 15];
  }
  return result;
}

class TemporaryConfig
{
public:
  explicit TemporaryConfig(int directory)
      : directory_(directory), name_(temporary_name()),
        descriptor_(::openat(
            directory, name_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600))
  {
  }
  ~TemporaryConfig()
  {
    if (!committed_ && ::unlinkat(directory_, name_.c_str(), 0) != 0)
      ::syslog(LOG_ERR, "Could not remove temporary macOS configuration");
  }
  int descriptor() const noexcept
  {
    return descriptor_.get();
  }
  void commit()
  {
    if (::fsync(descriptor_.get()) != 0)
      fail("Could not synchronize macOS configuration");
    if (::renameat(directory_, name_.c_str(), directory_, "config.json") != 0)
      fail("Could not atomically save configuration");
    committed_ = true;
    if (::fsync(directory_) != 0)
      throw ConfigurationCommitError(errno);
  }

private:
  const int directory_;
  const std::string name_;
  Descriptor descriptor_;
  bool committed_ = false;
};
} // namespace

std::optional<std::string> read_configuration_file(const std::filesystem::path& directory)
{
  const auto folder = open_directory(directory);
  const int raw =
      ::openat(folder.get(), "config.json", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
  if (raw < 0 && errno == ENOENT)
    return std::nullopt;
  const Descriptor file(raw);
  struct stat status{};
  if (::fstat(file.get(), &status) != 0)
    fail("Could not inspect macOS configuration");
  if (!S_ISREG(status.st_mode) || status.st_uid != ::geteuid() || (status.st_mode & 0077) != 0)
    throw std::runtime_error("Configuration must be a regular owner-restricted file");
  std::string contents;
  std::array<char, 4096> buffer{};
  for (;;)
  {
    const auto count = ::read(file.get(), buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0)
      fail("Could not read macOS configuration");
    if (count == 0)
      return contents;
    if (contents.size() + static_cast<std::size_t>(count) > maximum_configuration_bytes)
      throw std::runtime_error("Configuration exceeds the maximum size");
    contents.append(buffer.data(), static_cast<std::size_t>(count));
  }
}

void write_configuration_file(const std::filesystem::path& directory, std::string_view contents)
{
  if (contents.size() > maximum_configuration_bytes)
    throw std::invalid_argument("Configuration exceeds the maximum size");
  const auto folder = open_directory(directory);
  TemporaryConfig temporary(folder.get());
  while (!contents.empty())
  {
    const auto count = ::write(temporary.descriptor(), contents.data(), contents.size());
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      fail("Could not write macOS configuration");
    contents.remove_prefix(static_cast<std::size_t>(count));
  }
  temporary.commit();
}
} // namespace studio_remote::macos
