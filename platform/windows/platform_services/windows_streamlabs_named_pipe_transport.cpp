#include "windows_streamlabs_named_pipe_transport.hpp"

#include "windows_streamlabs_installations.hpp"
#include <studio_remote/streamlabs/streamlabs_rpc_transport.hpp>

#include <aclapi.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <studio_remote/streamlabs/streamlabs_protocol.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

namespace studio_remote::windows
{
namespace
{

using namespace std::chrono_literals;
using studio_remote::streamlabs::maximum_message_bytes;

constexpr wchar_t pipe_path[] = L"\\\\.\\pipe\\slobs";
constexpr auto connect_timeout = 3s;
constexpr auto request_timeout = 5s;
class Handle
{
public:
  explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value)
  {
  }

  ~Handle()
  {
    reset();
  }

  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;

  HANDLE get() const noexcept
  {
    return value_;
  }

  HANDLE release() noexcept
  {
    return std::exchange(value_, INVALID_HANDLE_VALUE);
  }

  void reset(HANDLE replacement = INVALID_HANDLE_VALUE) noexcept
  {
    if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr)
      CloseHandle(value_);
    value_ = replacement;
  }

private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

std::wstring lower(std::wstring value)
{
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
  return value;
}

std::wstring process_path(DWORD process_id)
{
  Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
  if (process.get() == INVALID_HANDLE_VALUE)
    return {};
  std::array<wchar_t, 32'768> path{};
  DWORD characters = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &characters))
    return {};
  return std::wstring(path.data(), characters);
}

std::vector<unsigned char> token_user(HANDLE process)
{
  HANDLE raw_token = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &raw_token))
    return {};
  Handle token(raw_token);
  DWORD bytes = 0;
  GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
  if (bytes == 0)
    return {};
  std::vector<unsigned char> result(bytes);
  if (!GetTokenInformation(token.get(), TokenUser, result.data(), bytes, &bytes))
    return {};
  return result;
}

bool same_user_process(DWORD process_id)
{
  Handle server(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
  if (server.get() == INVALID_HANDLE_VALUE)
    return false;
  const auto current = token_user(GetCurrentProcess());
  const auto peer = token_user(server.get());
  if (current.empty() || peer.empty())
    return false;
  const auto* current_user = reinterpret_cast<const TOKEN_USER*>(current.data());
  const auto* peer_user = reinterpret_cast<const TOKEN_USER*>(peer.data());
  return EqualSid(current_user->User.Sid, peer_user->User.Sid) != FALSE;
}

bool same_session_process(DWORD process_id)
{
  DWORD current_session = 0;
  DWORD server_session = 0;
  return ProcessIdToSessionId(GetCurrentProcessId(), &current_session) &&
         ProcessIdToSessionId(process_id, &server_session) && current_session == server_session;
}

bool pipe_owner_is_current_user(HANDLE pipe)
{
  PSID owner = nullptr;
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const auto status = GetSecurityInfo(pipe,
                                      SE_KERNEL_OBJECT,
                                      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                      &owner,
                                      nullptr,
                                      &dacl,
                                      nullptr,
                                      &descriptor);
  if (status != ERROR_SUCCESS || !descriptor || !owner)
    return false;
  const auto descriptor_guard = std::unique_ptr<void, decltype(&LocalFree)>(descriptor, LocalFree);

  const auto current = token_user(GetCurrentProcess());
  if (current.empty())
    return false;
  const auto* current_user = reinterpret_cast<const TOKEN_USER*>(current.data());
  if (!EqualSid(owner, current_user->User.Sid))
    return false;

  SID_IDENTIFIER_AUTHORITY world_authority = SECURITY_WORLD_SID_AUTHORITY;
  PSID world = nullptr;
  if (!AllocateAndInitializeSid(
          &world_authority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &world))
    return false;
  const auto world_guard = std::unique_ptr<void, decltype(&FreeSid)>(world, FreeSid);
  if (!dacl)
    return false;
  for (DWORD index = 0; index < dacl->AceCount; ++index)
  {
    void* raw_ace = nullptr;
    if (!GetAce(dacl, index, &raw_ace))
      return false;
    const auto* header = static_cast<const ACE_HEADER*>(raw_ace);
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
      continue;
    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
    auto* sid = reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart));
    if (EqualSid(sid, world) &&
        (ace->Mask & (FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_WRITE | GENERIC_ALL)) != 0)
      return false;
  }
  return true;
}

void verify_peer(HANDLE pipe, const StreamlabsInstallation& installation)
{
  DWORD process_id = 0;
  if (!GetNamedPipeServerProcessId(pipe, &process_id) || process_id == 0)
    throw std::runtime_error("Streamlabs named-pipe server identity is unavailable");
  if (!same_user_process(process_id) || !same_session_process(process_id))
    throw std::runtime_error("Streamlabs named-pipe server is owned by another user or session");
  std::error_code error;
  const auto actual = std::filesystem::weakly_canonical(process_path(process_id), error);
  const auto expected = std::filesystem::weakly_canonical(installation.executable, error);
  if (error || lower(actual.wstring()) != lower(expected.wstring()))
    throw std::runtime_error(
        "Streamlabs named-pipe server path does not match the trusted install");
  if (!pipe_owner_is_current_user(pipe))
    throw std::runtime_error("Streamlabs named-pipe access policy is not owner restricted");
}

DWORD wait_overlapped(HANDLE pipe, OVERLAPPED& operation, std::chrono::milliseconds timeout)
{
  const auto wait = WaitForSingleObject(operation.hEvent, static_cast<DWORD>(timeout.count()));
  if (wait != WAIT_OBJECT_0)
  {
    CancelIoEx(pipe, &operation);
    WaitForSingleObject(operation.hEvent, INFINITE);
    throw std::runtime_error(wait == WAIT_TIMEOUT ? "Streamlabs named-pipe operation timed out"
                                                  : "Streamlabs named-pipe wait failed");
  }
  DWORD transferred = 0;
  if (!GetOverlappedResult(pipe, &operation, &transferred, FALSE))
    throw std::runtime_error("Streamlabs named-pipe operation failed");
  return transferred;
}

} // namespace

class WindowsStreamlabsPipeChannel final : public studio_remote::streamlabs::IStreamlabsRpcChannel
{
public:
  ~WindowsStreamlabsPipeChannel() override
  {
    disconnect();
  }

  void connect() override
  {
    std::lock_guard lock(mutex_);
    if (pipe_.get() != INVALID_HANDLE_VALUE)
      return;
    const auto installation = detect_supported_streamlabs_installation();
    if (!installation)
      throw std::runtime_error(
          "A supported Streamlabs Desktop 1.21.9 or newer installation was not found");
    if (!installation->running)
      throw std::runtime_error("Streamlabs Desktop is not running from the trusted installation");

    const auto deadline = std::chrono::steady_clock::now() + connect_timeout;
    HANDLE raw_pipe = INVALID_HANDLE_VALUE;
    DWORD open_error = ERROR_SUCCESS;
    while (std::chrono::steady_clock::now() < deadline)
    {
      raw_pipe = CreateFileW(pipe_path,
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             nullptr,
                             OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED,
                             nullptr);
      if (raw_pipe != INVALID_HANDLE_VALUE)
        break;
      open_error = GetLastError();
      if (open_error == ERROR_ACCESS_DENIED)
        break;
      if (open_error != ERROR_PIPE_BUSY || !WaitNamedPipeW(pipe_path, 200))
        Sleep(50);
    }
    if (raw_pipe == INVALID_HANDLE_VALUE)
    {
      if (open_error == ERROR_ACCESS_DENIED)
        throw std::runtime_error(
            "Streamlabs named-pipe access was denied; Companion and Streamlabs Desktop must run "
            "as the same Windows user at compatible integrity levels");
      throw std::runtime_error("Streamlabs named pipe is unavailable");
    }
    Handle candidate(raw_pipe);
    verify_peer(candidate.get(), *installation);
    pipe_.reset(candidate.release());
    installation_ = *installation;
    receive_buffer_.clear();
  }

  void disconnect() noexcept override
  {
    std::lock_guard lock(mutex_);
    if (pipe_.get() != INVALID_HANDLE_VALUE)
      CancelIoEx(pipe_.get(), nullptr);
    pipe_.reset();
    receive_buffer_.clear();
  }

  bool connected() const noexcept override
  {
    std::lock_guard lock(mutex_);
    return pipe_.get() != INVALID_HANDLE_VALUE;
  }

  std::string desktop_version() const override
  {
    std::lock_guard lock(mutex_);
    return installation_.version;
  }

  void write_message(std::string_view message) override
  {
    const auto value = std::string(message) + '\n';
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (event.get() == nullptr || event.get() == INVALID_HANDLE_VALUE)
      throw std::runtime_error("Streamlabs named-pipe write event creation failed");
    OVERLAPPED operation{};
    operation.hEvent = event.get();
    DWORD written = 0;
    const auto started = WriteFile(
        pipe_.get(), value.data(), static_cast<DWORD>(value.size()), &written, &operation);
    if (!started && GetLastError() != ERROR_IO_PENDING)
      throw std::runtime_error("Streamlabs named-pipe write failed");
    if (!started)
      written = wait_overlapped(pipe_.get(), operation, request_timeout);
    if (written != value.size())
      throw std::runtime_error("Streamlabs named-pipe write was incomplete");
  }

  std::string read_message(std::chrono::milliseconds timeout) override
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
      if (const auto newline = receive_buffer_.find('\n'); newline != std::string::npos)
      {
        auto line = receive_buffer_.substr(0, newline);
        receive_buffer_.erase(0, newline + 1);
        if (line.empty())
          continue;
        return line;
      }
      std::array<char, 64 * 1'024> chunk{};
      Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
      if (event.get() == nullptr || event.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Streamlabs named-pipe read event creation failed");
      OVERLAPPED operation{};
      operation.hEvent = event.get();
      DWORD read = 0;
      const auto started =
          ReadFile(pipe_.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read, &operation);
      if (!started && GetLastError() != ERROR_IO_PENDING)
        throw std::runtime_error("Streamlabs named-pipe read failed");
      if (!started)
      {
        const auto remaining = std::max(std::chrono::milliseconds::zero(),
                                        std::chrono::duration_cast<std::chrono::milliseconds>(
                                            deadline - std::chrono::steady_clock::now()));
        read = wait_overlapped(pipe_.get(), operation, remaining);
      }
      if (read == 0)
        throw std::runtime_error("Streamlabs named pipe closed during a response");
      receive_buffer_.append(chunk.data(), read);
      if (receive_buffer_.size() > maximum_message_bytes)
        throw std::runtime_error("Streamlabs named-pipe frame exceeds the configured limit");
    }
  }

private:
  mutable std::mutex mutex_;
  Handle pipe_;
  StreamlabsInstallation installation_;
  std::string receive_buffer_;
};

WindowsStreamlabsNamedPipeTransport::WindowsStreamlabsNamedPipeTransport()
    : impl_(std::make_unique<studio_remote::streamlabs::StreamlabsRpcTransport>(
          std::make_unique<WindowsStreamlabsPipeChannel>()))
{
}

WindowsStreamlabsNamedPipeTransport::~WindowsStreamlabsNamedPipeTransport() = default;

void WindowsStreamlabsNamedPipeTransport::connect()
{
  impl_->connect();
}

void WindowsStreamlabsNamedPipeTransport::disconnect() noexcept
{
  impl_->disconnect();
}

bool WindowsStreamlabsNamedPipeTransport::connected() const noexcept
{
  return impl_->connected();
}

std::string WindowsStreamlabsNamedPipeTransport::desktop_version() const
{
  return impl_->desktop_version();
}

studio_remote::streamlabs::State WindowsStreamlabsNamedPipeTransport::read_state()
{
  return impl_->read_state();
}

bool WindowsStreamlabsNamedPipeTransport::read_studio_mode_enabled()
{
  return impl_->read_studio_mode_enabled();
}

std::string WindowsStreamlabsNamedPipeTransport::read_active_scene_id()
{
  return impl_->read_active_scene_id();
}

std::pair<studio_remote::streamlabs::OutputState, studio_remote::streamlabs::OutputState>
WindowsStreamlabsNamedPipeTransport::read_output_states()
{
  return impl_->read_output_states();
}

studio_remote::streamlabs::PerformanceState WindowsStreamlabsNamedPipeTransport::read_performance()
{
  return impl_->read_performance();
}

bool WindowsStreamlabsNamedPipeTransport::read_scene_item_visibility(std::string_view resource_id)
{
  return impl_->read_scene_item_visibility(resource_id);
}

studio_remote::streamlabs::AudioSource
WindowsStreamlabsNamedPipeTransport::read_audio_source(std::string_view resource_id)
{
  return impl_->read_audio_source(resource_id);
}

std::string WindowsStreamlabsNamedPipeTransport::read_program_scene_id()
{
  return impl_->read_program_scene_id();
}

void WindowsStreamlabsNamedPipeTransport::select_program_scene(std::string_view scene_id)
{
  impl_->select_program_scene(scene_id);
}

void WindowsStreamlabsNamedPipeTransport::select_preview_scene(std::string_view scene_id)
{
  impl_->select_preview_scene(scene_id);
}

void WindowsStreamlabsNamedPipeTransport::set_scene_item_visibility(std::string_view resource_id,
                                                                    bool visible)
{
  impl_->set_scene_item_visibility(resource_id, visible);
}

void WindowsStreamlabsNamedPipeTransport::set_audio_mute(std::string_view resource_id, bool muted)
{
  impl_->set_audio_mute(resource_id, muted);
}

void WindowsStreamlabsNamedPipeTransport::set_audio_volume(std::string_view resource_id,
                                                           double volume_db)
{
  impl_->set_audio_volume(resource_id, volume_db);
}

void WindowsStreamlabsNamedPipeTransport::toggle_recording()
{
  impl_->toggle_recording();
}

void WindowsStreamlabsNamedPipeTransport::toggle_streaming()
{
  impl_->toggle_streaming();
}

void WindowsStreamlabsNamedPipeTransport::execute_studio_transition()
{
  impl_->execute_studio_transition();
}

void WindowsStreamlabsNamedPipeTransport::subscribe(EventCallback callback)
{
  impl_->subscribe(std::move(callback));
}

std::optional<std::vector<studio_remote::streamlabs::ConfiguredAction>>
WindowsStreamlabsNamedPipeTransport::read_configured_actions()
{
  return impl_->read_configured_actions();
}
void WindowsStreamlabsNamedPipeTransport::set_studio_mode(bool enabled)
{
  impl_->set_studio_mode(enabled);
}
void WindowsStreamlabsNamedPipeTransport::select_collection(std::string_view id)
{
  impl_->select_collection(id);
}
} // namespace studio_remote::windows
