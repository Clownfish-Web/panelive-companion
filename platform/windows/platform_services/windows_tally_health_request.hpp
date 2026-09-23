#pragma once

#include <Windows.h>
#include <winhttp.h>

#include <studio_remote/tally/service_health.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

namespace studio_remote::windows
{
struct TallyHealthHandleCloser
{
  void operator()(void* handle) const noexcept;
};
using TallyHealthHandle = std::unique_ptr<void, TallyHealthHandleCloser>;

// The probe owner initiates one operation at a time. Native callbacks only complete its mailbox.
// Cancellation closes the request and joins HANDLE_CLOSING before destroying context/buffers.
class WindowsTallyHealthRequest final
{
public:
  WindowsTallyHealthRequest(HINTERNET connection, const std::wstring& path);
  ~WindowsTallyHealthRequest();
  bool configure();
  tally::ServiceHealth read(std::stop_token stop, std::chrono::steady_clock::time_point deadline);

private:
  static void CALLBACK callback(HINTERNET, DWORD_PTR, DWORD, void*, DWORD) noexcept;
  bool wait(DWORD expected, std::stop_token stop, std::chrono::steady_clock::time_point deadline);
  tally::ServiceHealth
  read_body(unsigned status, std::stop_token stop, std::chrono::steady_clock::time_point deadline);
  TallyHealthHandle request_;
  bool callback_bound_{false};
  std::mutex mutex_;
  std::condition_variable_any completed_;
  bool closed_{false};
  bool failed_{false};
  DWORD completion_{0};
  DWORD read_bytes_{0};
  std::array<char, 4096> buffer_{};
};
} // namespace studio_remote::windows
