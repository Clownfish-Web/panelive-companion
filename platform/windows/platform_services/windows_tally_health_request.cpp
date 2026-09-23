#include "windows_tally_health_request.hpp"

#include <studio_remote/tally/protocol.hpp>

#include <stdexcept>

namespace studio_remote::windows
{
using ProbeClock = std::chrono::steady_clock;
using tally::ServiceHealth;

void TallyHealthHandleCloser::operator()(void* handle) const noexcept
{
  if (!WinHttpCloseHandle(handle))
  {
    OutputDebugStringW(L"Could not release remote tally health request handle.\n");
    std::terminate();
  }
}

WindowsTallyHealthRequest::WindowsTallyHealthRequest(HINTERNET connection, const std::wstring& path)
    : request_(WinHttpOpenRequest(connection,
                                  L"GET",
                                  path.c_str(),
                                  nullptr,
                                  WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH))
{
  if (!request_)
    throw std::runtime_error("Could not create remote tally health request");
}

WindowsTallyHealthRequest::~WindowsTallyHealthRequest()
{
  request_.reset();
  if (callback_bound_)
  {
    std::unique_lock lock(mutex_);
    completed_.wait(lock, [this] { return closed_; });
  }
}

bool WindowsTallyHealthRequest::configure()
{
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  DWORD disable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
  DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
  DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
  if (!WinHttpSetOption(
          request_.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
      !WinHttpSetOption(
          request_.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) ||
      !WinHttpSetOption(
          request_.get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon)) ||
      !WinHttpSetOption(request_.get(), WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
    return false;
  if (WinHttpSetStatusCallback(request_.get(),
                               callback,
                               WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
                                   WINHTTP_CALLBACK_FLAG_HANDLES,
                               0) == WINHTTP_INVALID_STATUS_CALLBACK)
    return false;
  callback_bound_ = true;
  return true;
}

ServiceHealth WindowsTallyHealthRequest::read(std::stop_token stop, ProbeClock::time_point deadline)
{
  if (stop.stop_requested() || ProbeClock::now() >= deadline)
    return ServiceHealth::unavailable;
  constexpr auto headers =
      L"Accept: application/json\r\nCache-Control: no-cache\r\nPragma: no-cache\r\n";
  if (!WinHttpSendRequest(request_.get(),
                          headers,
                          static_cast<DWORD>(-1),
                          WINHTTP_NO_REQUEST_DATA,
                          0,
                          0,
                          reinterpret_cast<DWORD_PTR>(this)) ||
      !wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, stop, deadline) ||
      !WinHttpReceiveResponse(request_.get(), nullptr) ||
      !wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, stop, deadline))
    return ServiceHealth::unavailable;
  DWORD status = 0;
  DWORD size = sizeof(status);
  if (!WinHttpQueryHeaders(request_.get(),
                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX,
                           &status,
                           &size,
                           WINHTTP_NO_HEADER_INDEX))
    return ServiceHealth::unavailable;
  if (status != 200)
    return ServiceHealth::unhealthy;
  return read_body(status, stop, deadline);
}

ServiceHealth WindowsTallyHealthRequest::read_body(unsigned status,
                                                   std::stop_token stop,
                                                   ProbeClock::time_point deadline)
{
  std::array<wchar_t, 128> content_type{};
  DWORD size = static_cast<DWORD>(sizeof(content_type));
  if (!WinHttpQueryHeaders(request_.get(),
                           WINHTTP_QUERY_CONTENT_TYPE,
                           WINHTTP_HEADER_NAME_BY_INDEX,
                           content_type.data(),
                           &size,
                           WINHTTP_NO_HEADER_INDEX))
    return ServiceHealth::invalid_response;
  std::string type;
  for (const auto character : std::wstring_view(content_type.data()))
  {
    if (character > 127)
      return ServiceHealth::invalid_response;
    type.push_back(static_cast<char>(character));
  }
  std::string body;
  while (!stop.stop_requested() && ProbeClock::now() < deadline)
  {
    if (!WinHttpReadData(
            request_.get(), buffer_.data(), static_cast<DWORD>(buffer_.size()), nullptr) ||
        !wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, stop, deadline))
      return ServiceHealth::unavailable;
    if (read_bytes_ == 0)
      return remote_tally_protocol::parse_health(status, type, body);
    if (read_bytes_ > buffer_.size() || body.size() + read_bytes_ > 4096)
      return ServiceHealth::invalid_response;
    body.append(buffer_.data(), read_bytes_);
  }
  return ServiceHealth::unavailable;
}

void CALLBACK WindowsTallyHealthRequest::callback(
    HINTERNET, DWORD_PTR context, DWORD status, void*, DWORD length) noexcept
{
  if (!context)
    return;
  try
  {
    auto& self = *reinterpret_cast<WindowsTallyHealthRequest*>(context);
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
  catch (...)
  {
    OutputDebugStringW(L"Remote tally health completion failed.\n");
    std::terminate();
  }
}

bool WindowsTallyHealthRequest::wait(DWORD expected,
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
