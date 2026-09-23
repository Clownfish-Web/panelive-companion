#pragma once

#include <Windows.h>

#include <chrono>
#include <memory>
#include <mutex>

namespace studio_remote::windows
{

class ShutdownSignal
{
public:
  ShutdownSignal();
  ~ShutdownSignal();

  ShutdownSignal(const ShutdownSignal&) = delete;
  ShutdownSignal& operator=(const ShutdownSignal&) = delete;

  void wait();
  bool wait_for(std::chrono::milliseconds timeout);
  void request_stop();

private:
  static BOOL WINAPI handle_console_signal(DWORD signal);
  // Win32 supplies no callback context; this lock joins active callbacks before destruction.
  static std::mutex active_mutex_;
  static ShutdownSignal* active_instance_;

  struct EventDeleter
  {
    void operator()(void* event) const noexcept;
  };

  std::unique_ptr<void, EventDeleter> event_;
};

} // namespace studio_remote::windows
