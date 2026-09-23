#include "macos_shutdown_signal.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <optional>
#include <stdexcept>
#include <sys/event.h>
#include <syslog.h>
#include <system_error>
#include <unistd.h>

namespace studio_remote::macos
{
namespace
{
[[noreturn]] void fail(const char* operation)
{
  throw std::system_error(errno, std::generic_category(), operation);
}

class EventQueue final
{
public:
  EventQueue() : descriptor_(kqueue())
  {
    if (descriptor_ < 0)
      fail("Could not create companion shutdown queue");
  }
  ~EventQueue()
  {
    if (close(descriptor_) != 0)
      syslog(LOG_ERR, "Could not close companion shutdown queue");
  }
  EventQueue(const EventQueue&) = delete;
  EventQueue& operator=(const EventQueue&) = delete;
  int get() const noexcept
  {
    return descriptor_;
  }

private:
  const int descriptor_;
};

// kqueue records delivery after this handler returns. A caught (rather than ignored) disposition
// also resets to the default on exec, so spawned programs do not inherit ignored termination.
void receive_signal(int) noexcept
{
}

class SignalDisposition final
{
public:
  explicit SignalDisposition(int number) : number_(number)
  {
    if (sigaction(number_, nullptr, &previous_) != 0)
      fail("Could not inspect companion termination signal");
    if (previous_.sa_handler == receive_signal)
      throw std::logic_error("Companion shutdown signal already has an owner");
    struct sigaction action{};
    action.sa_handler = receive_signal;
    if (sigemptyset(&action.sa_mask) != 0 || sigaction(number_, &action, nullptr) != 0)
      fail("Could not register companion termination signal");
  }
  ~SignalDisposition()
  {
    if (sigaction(number_, &previous_, nullptr) != 0)
      syslog(LOG_ERR, "Could not restore companion termination signal");
  }
  SignalDisposition(const SignalDisposition&) = delete;
  SignalDisposition& operator=(const SignalDisposition&) = delete;

private:
  const int number_;
  struct sigaction previous_{};
};
} // namespace

struct MacosShutdownSignal::Impl
{
  Impl()
  {
    const auto flags = fcntl(queue.get(), F_GETFD);
    if (flags < 0 || fcntl(queue.get(), F_SETFD, flags | FD_CLOEXEC) != 0)
      fail("Could not restrict companion shutdown descriptor");
    struct kevent events[3]{};
    EV_SET(&events[0], SIGINT, EVFILT_SIGNAL, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    EV_SET(&events[1], SIGTERM, EVFILT_SIGNAL, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    EV_SET(&events[2], 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (kevent(queue.get(), events, 3, nullptr, 0, nullptr) < 0)
      fail("Could not register companion shutdown events");
    interrupt.emplace(SIGINT);
    terminate.emplace(SIGTERM);
    // A peer closing a socket must produce an I/O error, not terminate the process. There is
    // deliberately no shutdown queue subscription for SIGPIPE.
    broken_pipe.emplace(SIGPIPE);
  }
  EventQueue queue;
  std::optional<SignalDisposition> interrupt;
  std::optional<SignalDisposition> terminate;
  std::optional<SignalDisposition> broken_pipe;
  std::atomic<bool> stopped{false};
};

MacosShutdownSignal::MacosShutdownSignal() : impl_(std::make_unique<Impl>())
{
}
MacosShutdownSignal::~MacosShutdownSignal() = default;

void MacosShutdownSignal::request_stop()
{
  struct kevent event{};
  EV_SET(&event, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
  if (kevent(impl_->queue.get(), &event, 1, nullptr, 0, nullptr) < 0)
    fail("Could not signal companion shutdown");
  impl_->stopped.store(true, std::memory_order_release);
}

bool MacosShutdownSignal::wait_for(std::chrono::milliseconds timeout)
{
  if (timeout < std::chrono::milliseconds::zero() || timeout > std::chrono::hours(24))
    throw std::invalid_argument("Companion shutdown timeout is outside the supported range");
  if (impl_->stopped.load(std::memory_order_acquire))
    return true;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (true)
  {
    const auto remaining = std::max(std::chrono::steady_clock::duration::zero(),
                                    deadline - std::chrono::steady_clock::now());
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
    const timespec wait_time{nanoseconds / 1'000'000'000, nanoseconds % 1'000'000'000};
    struct kevent event{};
    const auto count = kevent(impl_->queue.get(), nullptr, 0, &event, 1, &wait_time);
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0)
      fail("Could not wait for companion shutdown");
    if (count == 0)
      return impl_->stopped.load(std::memory_order_acquire);
    if ((event.flags & EV_ERROR) != 0)
      throw std::system_error(
          static_cast<int>(event.data), std::generic_category(), "Companion shutdown event failed");
    impl_->stopped.store(true, std::memory_order_release);
    return true;
  }
}
void MacosShutdownSignal::wait()
{
  while (!wait_for(std::chrono::hours(24)))
  {
  }
}
} // namespace studio_remote::macos
