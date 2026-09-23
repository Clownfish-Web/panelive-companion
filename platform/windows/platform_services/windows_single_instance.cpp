#include "windows_single_instance.hpp"

#include <system_error>

namespace studio_remote::windows
{
namespace
{
constexpr wchar_t companion_mutex_name[] =
    L"Global\\ClownfishWeb.Panelive.Companion.SingleInstance.v1";
}

WindowsSingleInstance::WindowsSingleInstance() : WindowsSingleInstance(companion_mutex_name)
{
}

WindowsSingleInstance::WindowsSingleInstance(std::wstring mutex_name)
{
  mutex_ = CreateMutexW(nullptr, TRUE, mutex_name.c_str());
  if (!mutex_)
    throw std::system_error(
        GetLastError(), std::system_category(), "Could not create the companion instance mutex");

  if (GetLastError() == ERROR_ALREADY_EXISTS)
  {
    CloseHandle(mutex_);
    mutex_ = nullptr;
  }

  const auto event_name = mutex_name + L".ManagementActivation";
  management_event_ = CreateEventW(nullptr, FALSE, FALSE, event_name.c_str());
  if (!management_event_)
  {
    const auto error = GetLastError();
    if (mutex_)
    {
      ReleaseMutex(mutex_);
      CloseHandle(mutex_);
      mutex_ = nullptr;
    }
    throw std::system_error(
        error, std::system_category(), "Could not create the companion management event");
  }
}

WindowsSingleInstance::~WindowsSingleInstance()
{
  if (management_event_)
    CloseHandle(management_event_);
  if (mutex_)
  {
    ReleaseMutex(mutex_);
    CloseHandle(mutex_);
  }
}

bool WindowsSingleInstance::owns_instance() const
{
  return mutex_ != nullptr;
}

void WindowsSingleInstance::signal_management_request() const
{
  // A shortcut launch is allowed to transfer foreground permission to the existing UI process.
  // Without this handoff Windows may show the management window behind the current application.
  static_cast<void>(AllowSetForegroundWindow(ASFW_ANY));
  if (!management_event_ || !SetEvent(management_event_))
    throw std::system_error(
        GetLastError(), std::system_category(), "Could not signal the companion management event");
}

bool WindowsSingleInstance::consume_management_request() const
{
  if (!management_event_)
    return false;
  const auto result = WaitForSingleObject(management_event_, 0);
  if (result == WAIT_FAILED)
    throw std::system_error(
        GetLastError(), std::system_category(), "Could not read the companion management event");
  return result == WAIT_OBJECT_0;
}

} // namespace studio_remote::windows
