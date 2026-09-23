#pragma once

#include <Windows.h>
#include <winhttp.h>

#include <studio_remote/obs/obs_websocket.hpp>

#include <array>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace studio_remote::windows
{
// Active callers retain this connection. Native callbacks borrow it until HANDLE_CLOSING;
// close joins those notifications before the resource and its I/O buffers can be destroyed.
class WinHttpObsSocketConnection
{
public:
  WinHttpObsSocketConnection() = default;
  ~WinHttpObsSocketConnection();
  bool open(const std::wstring& host, std::uint16_t port, std::chrono::milliseconds timeout);
  bool send(std::string message);
  ObsSocketRead receive(std::chrono::milliseconds timeout);
  void close();
  std::string last_error() const;

private:
  using Deadline = std::chrono::steady_clock::time_point;
  struct HandleDeleter
  {
    void operator()(void* handle) const noexcept;
  };
  using Handle = std::unique_ptr<void, HandleDeleter>;
  enum class Phase
  {
    connecting,
    open,
    closed
  };
  enum class Failure
  {
    none,
    transport,
    timeout,
    binary_message,
    receive_overflow,
    send_overflow
  };
  struct Completion
  {
    bool ready = false;
    WINHTTP_WEB_SOCKET_STATUS status{};
  };
  static void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status, void* info, DWORD size);
  void completed(DWORD status, void* info, DWORD size);
  bool initialize(const std::wstring& host, std::uint16_t port, int timeout);
  bool upgrade();
  bool begin(Completion& completion, const std::function<DWORD()>& operation);
  bool wait(Completion& completion, Deadline deadline);
  void fail(Failure failure, DWORD error = 0);
  bool available() const;

  // API initiation/handle release never overlap. Callbacks only acquire state_mutex_.
  std::mutex api_mutex_;
  mutable std::mutex state_mutex_;
  std::condition_variable changed_;
  Phase phase_ = Phase::connecting;
  Failure failure_ = Failure::none;
  DWORD native_error_ = 0;
  unsigned callback_handles_ = 0;
  Completion read_, write_;
  Handle session_, connection_, request_, socket_;
  std::mutex receiver_mutex_, sender_mutex_;
  // The receive owner retains pending reads and partial messages across timeout returns.
  bool read_pending_ = false;
  std::array<char, 64 * 1024> read_buffer_{};
  std::string incoming_, outgoing_;
};
} // namespace studio_remote::windows
