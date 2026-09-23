#include "linux_process.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace studio_remote::linux
{
ShutdownSignal::ShutdownSignal()
{
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0)
    throw std::runtime_error("Could not block process termination signals");
  signal_descriptor_ = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
  wake_descriptor_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (signal_descriptor_ < 0 || wake_descriptor_ < 0)
    throw std::runtime_error("Could not create Linux shutdown descriptors");
}

ShutdownSignal::~ShutdownSignal()
{
  if (signal_descriptor_ >= 0)
    close(signal_descriptor_);
  if (wake_descriptor_ >= 0)
    close(wake_descriptor_);
}

bool ShutdownSignal::wait_for(std::chrono::milliseconds timeout)
{
  pollfd descriptors[] = {{signal_descriptor_, POLLIN, 0}, {wake_descriptor_, POLLIN, 0}};
  const auto result = poll(descriptors, 2, static_cast<int>(timeout.count()));
  if (result < 0 && errno != EINTR)
    throw std::runtime_error("Could not wait for Linux shutdown signal: " +
                             std::string(std::strerror(errno)));
  return result > 0;
}

void ShutdownSignal::request_stop()
{
  const std::uint64_t value = 1;
  ssize_t written = -1;
  do
  {
    written = write(wake_descriptor_, &value, sizeof(value));
  } while (written < 0 && errno == EINTR);
  if (written < 0 && errno != EAGAIN)
    throw std::runtime_error("Could not wake Linux shutdown signal: " +
                             std::string(std::strerror(errno)));
}

SingleInstance::SingleInstance(const std::filesystem::path& data_directory)
{
  const auto lock_path = data_directory / "companion.lock";
  descriptor_ = open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (descriptor_ < 0)
    throw std::runtime_error("Could not create Linux single-instance lock");
  owns_instance_ = flock(descriptor_, LOCK_EX | LOCK_NB) == 0;
}

SingleInstance::~SingleInstance()
{
  if (descriptor_ >= 0)
    close(descriptor_);
}

bool SingleInstance::owns_instance() const noexcept
{
  return owns_instance_;
}
} // namespace studio_remote::linux
