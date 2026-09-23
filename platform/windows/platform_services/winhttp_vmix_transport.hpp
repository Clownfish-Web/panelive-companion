#pragma once

#include <Windows.h>
#include <mutex>
#include <studio_remote/vmix/vmix_transport.hpp>
#include <winhttp.h>

namespace studio_remote::windows
{

class WinHttpVmixTransport final : public vmix::IVmixTransport
{
public:
  WinHttpVmixTransport(std::string host,
                       std::uint16_t port,
                       std::string username = {},
                       std::string password = {});
  ~WinHttpVmixTransport() override;

  void
  reconfigure(std::string host, std::uint16_t port, std::string username, std::string password);

  std::string read_state() override;
  std::string read_shortcuts() override;
  void trigger_shortcut(std::string_view identifier) override;
  void call(std::string_view function,
            const std::map<std::string, std::string>& parameters) override;

private:
  std::string get(std::string_view path);

  std::mutex mutex_;
  std::wstring username_;
  std::wstring password_;
  HINTERNET session_ = nullptr;
  HINTERNET connection_ = nullptr;
};

} // namespace studio_remote::windows
