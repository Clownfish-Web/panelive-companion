#include "winhttp_obs_socket_connection.hpp"
#include "windows_obs_connection.hpp"

#include <stdexcept>
#include <utility>

namespace studio_remote::windows
{
namespace
{
constexpr std::size_t maximum_message_bytes = 16 * 1024 * 1024;
DWORD checked(BOOL result)
{
  return result ? NO_ERROR : GetLastError();
}
} // namespace

void WinHttpObsSocketConnection::HandleDeleter::operator()(void* handle) const noexcept
{
  if (!WinHttpCloseHandle(handle))
  {
    // Losing a tracked handle without its final callback would invalidate live native context.
    OutputDebugStringW(L"Could not release an owned OBS WinHTTP handle.\n");
    std::terminate();
  }
}

WinHttpObsSocketConnection::~WinHttpObsSocketConnection()
{
  close();
}

void WinHttpObsSocketConnection::fail(Failure failure, DWORD error)
{
  std::lock_guard lock(state_mutex_);
  phase_ = Phase::closed;
  if (failure_ == Failure::none)
  {
    failure_ = failure;
    native_error_ = error;
  }
  changed_.notify_all();
}

bool WinHttpObsSocketConnection::available() const
{
  std::lock_guard lock(state_mutex_);
  return phase_ == Phase::open;
}

void CALLBACK WinHttpObsSocketConnection::callback(
    HINTERNET, DWORD_PTR context, DWORD status, void* info, DWORD size)
{
  if (!context)
    return;
  try
  {
    reinterpret_cast<WinHttpObsSocketConnection*>(context)->completed(status, info, size);
  }
  catch (...)
  {
    OutputDebugStringW(L"OBS WinHTTP completion bookkeeping failed.\n");
    std::terminate();
  }
}

void WinHttpObsSocketConnection::completed(DWORD status, void* info, DWORD size)
{
  std::lock_guard lock(state_mutex_);
  switch (status)
  {
  case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
    if (callback_handles_ == 0)
      std::terminate();
    --callback_handles_;
    break;
  case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
  case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
    write_.ready = true;
    break;
  case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
    read_.ready = true;
    break;
  case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
    if (info && size == sizeof(WINHTTP_WEB_SOCKET_STATUS))
    {
      read_.status = *static_cast<const WINHTTP_WEB_SOCKET_STATUS*>(info);
      read_.ready = true;
      break;
    }
    phase_ = Phase::closed;
    failure_ = Failure::transport;
    break;
  case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
    if (phase_ == Phase::closed)
      break;
    phase_ = Phase::closed;
    if (failure_ == Failure::none)
    {
      failure_ = Failure::transport;
      if (info && size == sizeof(WINHTTP_ASYNC_RESULT))
        native_error_ = static_cast<const WINHTTP_ASYNC_RESULT*>(info)->dwError;
    }
    break;
  default:
    break;
  }
  changed_.notify_all();
}

bool WinHttpObsSocketConnection::initialize(const std::wstring& host,
                                            std::uint16_t port,
                                            int timeout)
{
  std::lock_guard operation(api_mutex_);
  {
    std::lock_guard lock(state_mutex_);
    if (phase_ == Phase::closed)
      return false;
  }
  session_.reset(WinHttpOpen(L"panelive-companion/0.1",
                             WINHTTP_ACCESS_TYPE_NO_PROXY,
                             WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS,
                             WINHTTP_FLAG_ASYNC));
  if (!session_ || !WinHttpSetTimeouts(session_.get(), timeout, timeout, timeout, timeout) ||
      WinHttpSetStatusCallback(session_.get(),
                               callback,
                               WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
                                   WINHTTP_CALLBACK_FLAG_HANDLES,
                               0) == WINHTTP_INVALID_STATUS_CALLBACK)
  {
    fail(Failure::transport, GetLastError());
    return false;
  }
  connection_.reset(WinHttpConnect(session_.get(), host.c_str(), port, 0));
  if (connection_)
    request_.reset(WinHttpOpenRequest(connection_.get(),
                                      L"GET",
                                      L"/",
                                      nullptr,
                                      WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      0));
  DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
  if (!request_ ||
      !WinHttpSetOption(request_.get(), WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
  {
    fail(Failure::transport, GetLastError());
    return false;
  }
  {
    std::lock_guard lock(state_mutex_);
    ++callback_handles_;
  }
  if (!WinHttpSetOption(request_.get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
  {
    fail(Failure::transport, GetLastError());
    return false;
  }
  return true;
}

bool WinHttpObsSocketConnection::begin(Completion& completion,
                                       const std::function<DWORD()>& operation)
{
  std::lock_guard initiation(api_mutex_);
  {
    std::lock_guard lock(state_mutex_);
    if (phase_ == Phase::closed)
      return false;
    completion.ready = false;
    completion.status = {};
  }
  const auto error = operation();
  if (error == NO_ERROR || error == ERROR_IO_PENDING)
    return true;
  fail(Failure::transport, error);
  return false;
}

bool WinHttpObsSocketConnection::wait(Completion& completion, Deadline deadline)
{
  std::unique_lock lock(state_mutex_);
  if (!changed_.wait_until(
          lock, deadline, [&] { return phase_ == Phase::closed || completion.ready; }))
  {
    lock.unlock();
    fail(Failure::timeout);
    return false;
  }
  return phase_ != Phase::closed;
}

bool WinHttpObsSocketConnection::upgrade()
{
  std::lock_guard operation(api_mutex_);
  {
    std::lock_guard lock(state_mutex_);
    if (phase_ == Phase::closed)
      return false;
  }
  DWORD status = 0;
  DWORD size = sizeof(status);
  if (!WinHttpQueryHeaders(request_.get(),
                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX,
                           &status,
                           &size,
                           WINHTTP_NO_HEADER_INDEX) ||
      status != 101)
  {
    fail(Failure::transport, ERROR_WINHTTP_INVALID_SERVER_RESPONSE);
    return false;
  }
  socket_.reset(WinHttpWebSocketCompleteUpgrade(request_.get(), reinterpret_cast<DWORD_PTR>(this)));
  if (!socket_)
  {
    fail(Failure::transport, GetLastError());
    return false;
  }
  {
    std::lock_guard lock(state_mutex_);
    ++callback_handles_;
    if (phase_ == Phase::closed)
      return false;
    phase_ = Phase::open;
  }
  request_.reset();
  return true;
}

bool WinHttpObsSocketConnection::open(const std::wstring& host,
                                      std::uint16_t port,
                                      std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  if (initialize(host, port, static_cast<int>(timeout.count())) &&
      begin(write_,
            [this]
            {
              return checked(WinHttpSendRequest(request_.get(),
                                                WINHTTP_NO_ADDITIONAL_HEADERS,
                                                0,
                                                nullptr,
                                                0,
                                                0,
                                                reinterpret_cast<DWORD_PTR>(this)));
            }) &&
      wait(write_, deadline) &&
      begin(read_, [this] { return checked(WinHttpReceiveResponse(request_.get(), nullptr)); }) &&
      wait(read_, deadline) && upgrade())
    return true;
  close();
  return false;
}

bool WinHttpObsSocketConnection::send(std::string message)
{
  std::lock_guard sender(sender_mutex_);
  if (!available())
    return false;
  if (message.size() > maximum_message_bytes)
  {
    fail(Failure::send_overflow);
    close();
    return false;
  }
  outgoing_ = std::move(message);
  if (begin(write_,
            [this]
            {
              return WinHttpWebSocketSend(socket_.get(),
                                          WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                          outgoing_.data(),
                                          static_cast<DWORD>(outgoing_.size()));
            }) &&
      wait(write_, std::chrono::steady_clock::now() + std::chrono::seconds(5)))
    return true;
  close();
  return false;
}

ObsSocketRead WinHttpObsSocketConnection::receive(std::chrono::milliseconds timeout)
{
  std::lock_guard receiver(receiver_mutex_);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (available())
  {
    if (!read_pending_)
    {
      read_pending_ =
          begin(read_,
                [this]
                {
                  return WinHttpWebSocketReceive(socket_.get(),
                                                 read_buffer_.data(),
                                                 static_cast<DWORD>(read_buffer_.size()),
                                                 nullptr,
                                                 nullptr);
                });
      if (!read_pending_)
        break;
    }
    WINHTTP_WEB_SOCKET_STATUS status{};
    {
      std::unique_lock lock(state_mutex_);
      if (!changed_.wait_until(
              lock, deadline, [this] { return phase_ == Phase::closed || read_.ready; }))
        return {ObsSocketReadState::timeout, {}};
      if (phase_ == Phase::closed)
        break;
      status = read_.status;
    }
    read_pending_ = false;
    if (status.eBufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
      break;
    if (status.eBufferType != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE &&
        status.eBufferType != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE)
    {
      fail(Failure::binary_message);
      break;
    }
    if (status.dwBytesTransferred > read_buffer_.size() ||
        status.dwBytesTransferred > maximum_message_bytes - incoming_.size())
    {
      fail(Failure::receive_overflow);
      break;
    }
    incoming_.append(read_buffer_.data(), status.dwBytesTransferred);
    if (status.eBufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
      return {ObsSocketReadState::message, std::exchange(incoming_, {})};
  }
  close();
  return {};
}

void WinHttpObsSocketConnection::close()
{
  {
    std::lock_guard operation(api_mutex_);
    fail(Failure::none);
    socket_.reset();
    request_.reset();
    connection_.reset();
    session_.reset();
  }
  std::unique_lock lock(state_mutex_);
  changed_.wait(lock, [this] { return callback_handles_ == 0; });
}

std::string WinHttpObsSocketConnection::last_error() const
{
  std::lock_guard lock(state_mutex_);
  switch (failure_)
  {
  case Failure::none:
    return {};
  case Failure::transport:
    return expected_production_backend_offline_error(native_error_)
               ? std::string{}
               : "OBS WebSocket transport failed: " + std::to_string(native_error_);
  case Failure::timeout:
    return "OBS WebSocket operation timed out";
  case Failure::binary_message:
    return "OBS WebSocket received a binary message";
  case Failure::receive_overflow:
    return "OBS WebSocket receive message is too large";
  case Failure::send_overflow:
    return "OBS WebSocket send message is too large";
  }
  return "OBS WebSocket transport failed";
}
} // namespace studio_remote::windows
