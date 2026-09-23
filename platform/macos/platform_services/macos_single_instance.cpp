#include "macos_single_instance.hpp"

#include "macos_platform_services.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <notify.h>
#include <optional>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <syslog.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace studio_remote::macos
{
namespace
{
[[noreturn]] void fail(const char* operation)
{
  throw std::system_error(errno, std::generic_category(), operation);
}
class Descriptor final
{
public:
  explicit Descriptor(int descriptor) : descriptor_(descriptor)
  {
    if (descriptor < 0)
      fail("Could not open companion instance storage");
  }
  ~Descriptor()
  {
    if (close(descriptor_) != 0)
      syslog(LOG_ERR, "Could not close companion instance descriptor");
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int get() const noexcept
  {
    return descriptor_;
  }

private:
  const int descriptor_;
};

class Registration final
{
public:
  ~Registration()
  {
    if (token_ >= 0 && notify_cancel(token_) != NOTIFY_STATUS_OK)
      syslog(LOG_ERR, "Could not cancel companion management event");
  }
  Registration() = default;
  Registration(const Registration&) = delete;
  Registration& operator=(const Registration&) = delete;
  std::uint32_t open(const char* name, int* descriptor)
  {
    return notify_register_file_descriptor(name, descriptor, 0, &token_);
  }

private:
  int token_ = -1;
};

class ActivationEvent final
{
public:
  explicit ActivationEvent(std::string name) : name_(std::move(name))
  {
    if (name_.empty() || name_.size() > 1024 || name_.find('\0') != std::string::npos)
      throw std::invalid_argument("Invalid companion activation name");
    int descriptor = -1;
    const auto result = registration_.open(name_.c_str(), &descriptor);
    if (descriptor >= 0)
      descriptor_.emplace(descriptor);
    if (result != NOTIFY_STATUS_OK || !descriptor_)
      throw std::runtime_error("Could not create companion management event");
    const auto flags = fcntl(descriptor, F_GETFL);
    const auto descriptor_flags = fcntl(descriptor, F_GETFD);
    if (flags < 0 || descriptor_flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0 ||
        fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0)
      fail("Could not configure companion management event");
  }
  bool consume() const
  {
    std::array<std::uint32_t, 32> messages{};
    ssize_t size;
    do
    {
      size = read(descriptor_->get(), messages.data(), sizeof(messages));
    } while (size < 0 && errno == EINTR);
    if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      return false;
    if (size < 0)
      fail("Could not read companion management event");
    if (size == 0 || size % sizeof(std::uint32_t) != 0)
      throw std::runtime_error("Invalid companion management notification");
    return true;
  }
  void signal() const
  {
    if (notify_post(name_.c_str()) != NOTIFY_STATUS_OK)
      throw std::runtime_error("Could not signal companion management event");
  }

private:
  const std::string name_;
  std::optional<Descriptor> descriptor_;
  Registration registration_; // Cancel delivery before closing its descriptor.
};

int open_lock(const std::filesystem::path& directory)
{
  std::filesystem::create_directories(directory);
  Descriptor folder(open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  struct stat status{};
  if (fstat(folder.get(), &status) != 0)
    fail("Could not inspect companion instance directory");
  if (status.st_uid != geteuid())
    throw std::runtime_error("Companion instance directory belongs to another user");
  if (fchmod(folder.get(), 0700) != 0)
    fail("Could not restrict companion instance directory");
  const auto descriptor = openat(
      folder.get(), "companion.lock", O_CREAT | O_RDWR | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (descriptor < 0)
    fail("Could not open companion instance lock");
  return descriptor;
}
bool claim_lock(int descriptor)
{
  struct stat status{};
  if (fstat(descriptor, &status) != 0)
    fail("Could not inspect companion instance lock");
  if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() || status.st_nlink != 1 ||
      (status.st_mode & 0077) != 0)
    throw std::runtime_error("Companion instance lock is not an owner-restricted regular file");
  int result;
  do
  {
    result = flock(descriptor, LOCK_EX | LOCK_NB);
  } while (result != 0 && errno == EINTR);
  if (result == 0)
    return true;
  if (errno == EWOULDBLOCK || errno == EAGAIN)
    return false;
  fail("Could not claim companion instance lock");
}
} // namespace

struct MacosSingleInstance::Impl
{
  Impl(const std::filesystem::path& directory, std::string activation_name)
      : descriptor(open_lock(directory)), owns(claim_lock(descriptor.get())),
        event(std::move(activation_name))
  {
  }
  Descriptor descriptor;
  const bool owns;
  ActivationEvent event;
};

MacosSingleInstance::MacosSingleInstance()
    : MacosSingleInstance(MacosPlatformServices{}.data_directory(),
                          "panelive-companion.management." + std::to_string(geteuid()))
{
}
MacosSingleInstance::MacosSingleInstance(std::filesystem::path directory,
                                         std::string activation_name)
    : impl_(std::make_unique<Impl>(directory, std::move(activation_name)))
{
}
MacosSingleInstance::~MacosSingleInstance() = default;
bool MacosSingleInstance::owns_instance() const noexcept
{
  return impl_->owns;
}
void MacosSingleInstance::signal_management_request() const
{
  impl_->event.signal();
}
bool MacosSingleInstance::consume_management_request() const
{
  return impl_->owns && impl_->event.consume();
}
} // namespace studio_remote::macos
