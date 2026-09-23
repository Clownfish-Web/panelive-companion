#include "windows_tally_health_probe.hpp"
#include "windows_tally_health_request.hpp"

#include <algorithm>
#include <stdexcept>

namespace studio_remote::windows
{
WindowsTallyHealthProbe::WindowsTallyHealthProbe(std::string url, std::chrono::milliseconds timeout)
    : url_(std::move(url)), timeout_(timeout)
{
  if (timeout.count() <= 0 || timeout > std::chrono::seconds(4))
    throw std::invalid_argument("Remote tally health deadline must be between 1 and 4000 ms");
}

tally::ServiceHealth WindowsTallyHealthProbe::probe(std::stop_token stop)
try
{
  using tally::ServiceHealth;
  const auto deadline = std::chrono::steady_clock::now() + timeout_;
  if (stop.stop_requested() || url_.size() > 2048 || !url_.starts_with("https://") ||
      std::ranges::any_of(
          url_, [](unsigned char character) { return character <= 32 || character >= 127; }))
    return ServiceHealth::unavailable;
  const std::wstring url(url_.begin(), url_.end());
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
      parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts) ||
      parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwHostNameLength == 0 ||
      parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0 || parts.dwExtraInfoLength != 0)
    return ServiceHealth::unavailable;
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
  TallyHealthHandle session(WinHttpOpen(L"panelive-companion/remote-tally-health",
                                        WINHTTP_ACCESS_TYPE_NO_PROXY,
                                        WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS,
                                        WINHTTP_FLAG_ASYNC));
  const auto timeout = static_cast<int>(timeout_.count());
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
  if (!session || !WinHttpSetTimeouts(session.get(), timeout, timeout, timeout, timeout) ||
      !WinHttpSetOption(
          session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)))
    return ServiceHealth::unavailable;
  TallyHealthHandle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
  if (!connection)
    return ServiceHealth::unavailable;
  WindowsTallyHealthRequest request(connection.get(), path);
  if (!request.configure())
    return ServiceHealth::unavailable;
  return request.read(stop, deadline);
}
catch (const std::exception&)
{
  return tally::ServiceHealth::unavailable;
}
} // namespace studio_remote::windows
