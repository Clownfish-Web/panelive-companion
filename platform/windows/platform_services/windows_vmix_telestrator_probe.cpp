#include "windows_vmix_telestrator_probe.hpp"
#include "windows_vmix_telestrator_request.hpp"

#include <Windows.h>
#include <winhttp.h>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace studio_remote::windows
{
namespace
{
using ProbeClock = std::chrono::steady_clock;
constexpr auto probe_timeout = std::chrono::seconds(4);

std::wstring wide(std::string_view text)
{
  if (text.empty())
    return {};
  if (text.size() > 65'536)
    throw std::invalid_argument("Invalid vMix Telestrator connection text");
  const auto size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0)
    throw std::invalid_argument("Invalid vMix Telestrator connection text");
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  if (MultiByteToWideChar(CP_UTF8,
                          MB_ERR_INVALID_CHARS,
                          text.data(),
                          static_cast<int>(text.size()),
                          result.data(),
                          size) != size)
    throw std::invalid_argument("Invalid vMix Telestrator connection text");
  return result;
}

class InternetHandle
{
public:
  explicit InternetHandle(HINTERNET handle) : handle_(handle)
  {
  }
  ~InternetHandle()
  {
    if (handle_ && !WinHttpCloseHandle(handle_))
      std::cerr << "Could not close vMix Telestrator HTTP handle\n";
  }
  InternetHandle(const InternetHandle&) = delete;
  InternetHandle& operator=(const InternetHandle&) = delete;
  HINTERNET get() const
  {
    return handle_;
  }

private:
  HINTERNET handle_;
};

} // namespace

WindowsVmixTelestratorProbe::WindowsVmixTelestratorProbe(std::string host,
                                                         std::uint16_t port,
                                                         std::string username,
                                                         std::string password,
                                                         std::string local_lan_address)
    : host_(std::move(host)), port_(port), username_(std::move(username)),
      password_(std::move(password)),
      public_url_(vmix_telestrator_public_url(host_, port_, local_lan_address))
{
}

TelestratorDescriptor WindowsVmixTelestratorProbe::probe(std::stop_token stop)
{
  if (public_url_.empty())
    return {};
  const TelestratorDescriptor failed{TelestratorStatus::backend_unavailable, {}};
  const auto deadline = ProbeClock::now() + probe_timeout;
  try
  {
    const auto host = wide(host_);
    const auto username = wide(username_);
    const auto password = wide(password_);
    InternetHandle session(WinHttpOpen(L"panelive-companion/telestrator",
                                       WINHTTP_ACCESS_TYPE_NO_PROXY,
                                       WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS,
                                       WINHTTP_FLAG_ASYNC));
    if (!session.get() || !WinHttpSetTimeouts(session.get(), 4'000, 4'000, 4'000, 4'000))
      return failed;
    InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), port_, 0));
    if (!connection.get())
      return failed;
    WindowsVmixTelestratorRequest request(connection.get());
    if (!request.configure(username, password))
      return failed;
    return request.read(public_url_, stop, deadline);
  }
  catch (const std::exception&)
  {
    // Boundary failures become the optional feature's sanitized status, never production errors.
    return failed;
  }
}
} // namespace studio_remote::windows
