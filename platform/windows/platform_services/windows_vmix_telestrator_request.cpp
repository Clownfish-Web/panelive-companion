#include "windows_vmix_telestrator_request.hpp"

#include <iostream>
#include <stdexcept>
#include <studio_remote/vmix/vmix_telestrator_discovery.hpp>

namespace studio_remote::windows
{
namespace
{
using ProbeClock = std::chrono::steady_clock;
constexpr std::size_t maximum_page_bytes = 1U << 20;
} // namespace

WindowsVmixTelestratorRequest::WindowsVmixTelestratorRequest(HINTERNET connection)
    : request_(WinHttpOpenRequest(connection,
                                  L"GET",
                                  L"/telestrator/",
                                  nullptr,
                                  WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  0))
{
  if (!request_)
    throw std::runtime_error("Could not create vMix Telestrator request");
}

WindowsVmixTelestratorRequest::~WindowsVmixTelestratorRequest()
{
  if (!WinHttpCloseHandle(request_))
    std::cerr << "Could not close vMix Telestrator request\n";
  if (callback_bound_)
  {
    std::unique_lock lock(mutex_);
    completed_.wait(lock, [this] { return closed_; });
  }
}

bool WindowsVmixTelestratorRequest::configure(const std::wstring& username,
                                              const std::wstring& password)
{
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  DWORD disable = WINHTTP_DISABLE_COOKIES;
  DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
  DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
  if (!WinHttpSetOption(request_, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
      !WinHttpSetOption(request_, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) ||
      !WinHttpSetOption(request_, WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon)) ||
      !WinHttpSetOption(request_, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
    return false;
  if (WinHttpSetStatusCallback(request_,
                               callback,
                               WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
                                   WINHTTP_CALLBACK_FLAG_HANDLES,
                               0) == WINHTTP_INVALID_STATUS_CALLBACK)
    return false;
  callback_bound_ = true;
  return username.empty() || WinHttpSetCredentials(request_,
                                                   WINHTTP_AUTH_TARGET_SERVER,
                                                   WINHTTP_AUTH_SCHEME_BASIC,
                                                   username.c_str(),
                                                   password.c_str(),
                                                   nullptr);
}

TelestratorDescriptor WindowsVmixTelestratorRequest::read(std::string_view public_url,
                                                          std::stop_token stop,
                                                          ProbeClock::time_point deadline)
{
  const TelestratorDescriptor failed{TelestratorStatus::backend_unavailable, {}};
  if (stop.stop_requested() || ProbeClock::now() >= deadline)
    return failed;
  if (!WinHttpSendRequest(request_,
                          WINHTTP_NO_ADDITIONAL_HEADERS,
                          0,
                          WINHTTP_NO_REQUEST_DATA,
                          0,
                          0,
                          reinterpret_cast<DWORD_PTR>(this)) ||
      !wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, stop, deadline))
    return failed;
  if (!WinHttpReceiveResponse(request_, nullptr) ||
      !wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, stop, deadline))
    return failed;
  DWORD status = 0;
  DWORD size = sizeof(status);
  if (!WinHttpQueryHeaders(request_,
                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX,
                           &status,
                           &size,
                           WINHTTP_NO_HEADER_INDEX))
    return failed;
  if (status != 200)
    return vmix::classify_telestrator_page(status, {}, public_url);
  std::string body;
  while (!stop.stop_requested() && ProbeClock::now() < deadline)
  {
    if (!WinHttpReadData(request_, buffer_.data(), static_cast<DWORD>(buffer_.size()), nullptr) ||
        !wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, stop, deadline))
      return failed;
    if (read_bytes_ == 0)
      return vmix::classify_telestrator_page(status, body, public_url);
    if (read_bytes_ > buffer_.size() || body.size() + read_bytes_ > maximum_page_bytes)
      return failed;
    body.append(buffer_.data(), read_bytes_);
  }
  return failed;
}

void CALLBACK WindowsVmixTelestratorRequest::callback(
    HINTERNET, DWORD_PTR context, DWORD status, void*, DWORD length) noexcept
{
  if (!context)
    return;
  auto& self = *reinterpret_cast<WindowsVmixTelestratorRequest*>(context);
  std::lock_guard lock(self.mutex_);
  if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
    self.closed_ = true;
  else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
    self.failed_ = true;
  else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
           status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
           status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
  {
    self.completion_ = status;
    if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
      self.read_bytes_ = length;
  }
  self.completed_.notify_all();
}

bool WindowsVmixTelestratorRequest::wait(DWORD expected,
                                         std::stop_token stop,
                                         ProbeClock::time_point deadline)
{
  std::unique_lock lock(mutex_);
  completed_.wait_until(
      lock, stop, deadline, [this, expected] { return failed_ || completion_ == expected; });
  const bool success =
      !stop.stop_requested() && !failed_ && completion_ == expected && ProbeClock::now() < deadline;
  completion_ = 0;
  return success;
}
} // namespace studio_remote::windows
