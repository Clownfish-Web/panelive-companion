#include "windows_shutdown_signal.hpp"

#include <stdexcept>
#include <system_error>

namespace studio_remote::windows
{

std::mutex ShutdownSignal::active_mutex_;
ShutdownSignal* ShutdownSignal::active_instance_ = nullptr;

namespace
{
[[noreturn]] void fail(const char* operation)
{
  throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}
} // namespace

void ShutdownSignal::EventDeleter::operator()(void* event) const noexcept
{
  if (!CloseHandle(event))
    OutputDebugStringW(L"Could not close the shutdown event.\n");
}

ShutdownSignal::ShutdownSignal()
{
  event_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!event_)
    fail("Could not create the shutdown event");

  std::lock_guard lock(active_mutex_);
  if (active_instance_)
    throw std::runtime_error("Only one shutdown signal may be active");

  active_instance_ = this;
  if (!SetConsoleCtrlHandler(handle_console_signal, TRUE))
  {
    active_instance_ = nullptr;
    fail("Could not register the Windows shutdown handler");
  }
}

ShutdownSignal::~ShutdownSignal()
{
  std::lock_guard lock(active_mutex_);
  active_instance_ = nullptr;
  if (!SetConsoleCtrlHandler(handle_console_signal, FALSE))
    OutputDebugStringW(L"Could not unregister the Windows shutdown handler.\n");
}

void ShutdownSignal::wait()
{
  if (WaitForSingleObject(event_.get(), INFINITE) != WAIT_OBJECT_0)
    fail("Could not wait for companion shutdown");
}

bool ShutdownSignal::wait_for(std::chrono::milliseconds timeout)
{
  if (timeout < std::chrono::milliseconds::zero() || timeout > std::chrono::hours(24))
    throw std::invalid_argument("Companion shutdown timeout is outside the supported range");
  const auto result = WaitForSingleObject(event_.get(), static_cast<DWORD>(timeout.count()));
  if (result == WAIT_FAILED)
    fail("Could not wait for companion shutdown");
  return result == WAIT_OBJECT_0;
}

void ShutdownSignal::request_stop()
{
  if (!SetEvent(event_.get()))
    fail("Could not signal companion shutdown");
}

BOOL WINAPI ShutdownSignal::handle_console_signal(DWORD signal)
{
  switch (signal)
  {
  case CTRL_C_EVENT:
  case CTRL_BREAK_EVENT:
  case CTRL_CLOSE_EVENT:
  case CTRL_LOGOFF_EVENT:
  case CTRL_SHUTDOWN_EVENT:
    try
    {
      std::lock_guard lock(active_mutex_);
      if (!active_instance_)
        return FALSE;
      active_instance_->request_stop();
      return TRUE;
    }
    catch (...)
    {
      OutputDebugStringW(L"Could not deliver Windows console shutdown.\n");
      return FALSE;
    }
  default:
    return FALSE;
  }
}

} // namespace studio_remote::windows
