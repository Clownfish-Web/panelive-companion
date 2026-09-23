#pragma once

#include <Windows.h>
#include <winhttp.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <studio_remote/provider/telestrator.hpp>

namespace studio_remote::windows
{
// The probe worker alone initiates operations. Callbacks complete a single-operation mailbox.
// Request buffers and context survive through HANDLE_CLOSING, including on cancellation.
class WindowsVmixTelestratorRequest final
{
public:
  explicit WindowsVmixTelestratorRequest(HINTERNET connection);
  ~WindowsVmixTelestratorRequest();
  WindowsVmixTelestratorRequest(const WindowsVmixTelestratorRequest&) = delete;
  WindowsVmixTelestratorRequest& operator=(const WindowsVmixTelestratorRequest&) = delete;

  bool configure(const std::wstring& username, const std::wstring& password);
  TelestratorDescriptor read(std::string_view public_url,
                             std::stop_token stop,
                             std::chrono::steady_clock::time_point deadline);

private:
  static void CALLBACK
  callback(HINTERNET, DWORD_PTR context, DWORD status, void*, DWORD length) noexcept;
  bool wait(DWORD expected, std::stop_token stop, std::chrono::steady_clock::time_point deadline);

  HINTERNET request_;
  bool callback_bound_ = false;
  std::mutex mutex_;
  std::condition_variable_any completed_;
  bool closed_ = false;
  bool failed_ = false;
  DWORD completion_ = 0;
  DWORD read_bytes_ = 0;
  std::array<char, 16'384> buffer_{};
};
} // namespace studio_remote::windows
