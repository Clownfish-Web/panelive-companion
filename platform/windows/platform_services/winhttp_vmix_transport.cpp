#include "winhttp_vmix_transport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace studio_remote::windows
{
namespace
{

constexpr std::size_t maximum_response_bytes = 1U << 20;

std::wstring wide(std::string_view value)
{
  if (value.empty())
    return {};
  const auto size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0)
    throw std::invalid_argument("vMix connection text is not valid UTF-8");
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8,
                      MB_ERR_INVALID_CHARS,
                      value.data(),
                      static_cast<int>(value.size()),
                      result.data(),
                      size);
  return result;
}

std::string percent_encode(std::string_view value)
{
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (const auto character : value)
  {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isalnum(byte) || byte == '-' || byte == '_' || byte == '.' || byte == '~')
      result.push_back(character);
    else
    {
      result.push_back('%');
      result.push_back(hex[byte >> 4]);
      result.push_back(hex[byte & 0x0f]);
    }
  }
  return result;
}

class InternetHandle
{
public:
  explicit InternetHandle(HINTERNET value) : value_(value)
  {
  }
  ~InternetHandle()
  {
    if (value_)
      WinHttpCloseHandle(value_);
  }
  operator HINTERNET() const noexcept
  {
    return value_;
  }

private:
  HINTERNET value_ = nullptr;
};

} // namespace

WinHttpVmixTransport::WinHttpVmixTransport(std::string host,
                                           std::uint16_t port,
                                           std::string username,
                                           std::string password)
    : username_(wide(username)), password_(wide(password))
{
  session_ = WinHttpOpen(L"panelive-companion/vmix",
                         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                         WINHTTP_NO_PROXY_NAME,
                         WINHTTP_NO_PROXY_BYPASS,
                         0);
  if (!session_)
    throw std::runtime_error("Could not create the vMix HTTP session");
  WinHttpSetTimeouts(session_, 3'000, 3'000, 3'000, 3'000);
  connection_ = WinHttpConnect(session_, wide(host).c_str(), port, 0);
  if (!connection_)
  {
    WinHttpCloseHandle(session_);
    session_ = nullptr;
    throw std::runtime_error("Could not create the vMix HTTP connection");
  }
}

WinHttpVmixTransport::~WinHttpVmixTransport()
{
  if (connection_)
    WinHttpCloseHandle(connection_);
  if (session_)
    WinHttpCloseHandle(session_);
}

void WinHttpVmixTransport::reconfigure(std::string host,
                                       std::uint16_t port,
                                       std::string username,
                                       std::string password)
{
  const auto wide_host = wide(host);
  const auto wide_username = wide(username);
  const auto wide_password = wide(password);
  std::lock_guard lock(mutex_);
  const auto replacement = WinHttpConnect(session_, wide_host.c_str(), port, 0);
  if (!replacement)
    throw std::runtime_error("Could not create the replacement vMix HTTP connection");
  const auto previous = connection_;
  connection_ = replacement;
  username_ = wide_username;
  password_ = wide_password;
  if (previous)
    WinHttpCloseHandle(previous);
}

std::string WinHttpVmixTransport::read_state()
{
  return get("/api/");
}

std::string WinHttpVmixTransport::read_shortcuts()
{
  return get("/");
}

void WinHttpVmixTransport::trigger_shortcut(std::string_view identifier)
{
  static_cast<void>(get("/?shortcut=" + percent_encode(identifier)));
}

void WinHttpVmixTransport::call(std::string_view function,
                                const std::map<std::string, std::string>& parameters)
{
  std::string path = "/api/?Function=" + percent_encode(function);
  for (const auto& [name, value] : parameters)
    path += "&" + percent_encode(name) + "=" + percent_encode(value);
  static_cast<void>(get(path));
}

std::string WinHttpVmixTransport::get(std::string_view path)
{
  std::lock_guard lock(mutex_);
  InternetHandle request(WinHttpOpenRequest(connection_,
                                            L"GET",
                                            wide(path).c_str(),
                                            nullptr,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            0));
  if (!request)
    throw std::runtime_error("Could not create a vMix HTTP request");
  if (!username_.empty() && !WinHttpSetCredentials(request,
                                                   WINHTTP_AUTH_TARGET_SERVER,
                                                   WINHTTP_AUTH_SCHEME_BASIC,
                                                   username_.c_str(),
                                                   password_.c_str(),
                                                   nullptr))
    throw std::runtime_error("Could not configure vMix HTTP credentials");
  if (!WinHttpSendRequest(
          request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr))
    throw std::runtime_error("vMix HTTP request failed: " + std::to_string(GetLastError()));

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (!WinHttpQueryHeaders(request,
                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX,
                           &status,
                           &status_size,
                           WINHTTP_NO_HEADER_INDEX))
    throw std::runtime_error("Could not read the vMix HTTP response status");
  std::string body;
  while (true)
  {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available))
      throw std::runtime_error("Could not read the vMix HTTP response length");
    if (available == 0)
      break;
    if (body.size() + available > maximum_response_bytes)
      throw std::runtime_error("vMix HTTP response exceeds the one MiB limit");
    const auto offset = body.size();
    body.resize(offset + available);
    DWORD received = 0;
    if (!WinHttpReadData(request, body.data() + offset, available, &received))
      throw std::runtime_error("Could not read the vMix HTTP response body");
    body.resize(offset + received);
  }
  if (status != 200)
  {
    body.erase(std::remove_if(body.begin(),
                              body.end(),
                              [](unsigned char character) { return character < 0x20; }),
               body.end());
    if (body.size() > 200)
      body.resize(200);
    throw std::runtime_error("vMix HTTP request returned status " + std::to_string(status) +
                             (body.empty() ? std::string{} : ": " + body));
  }
  return body;
}

} // namespace studio_remote::windows
