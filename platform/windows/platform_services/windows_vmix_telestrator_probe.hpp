#pragma once

#include <cstdint>
#include <studio_remote/vmix/vmix_telestrator_discovery.hpp>

namespace studio_remote::windows
{
// Only literal loopback/localhost may use the explicitly selected local LAN address.
std::string vmix_telestrator_public_url(std::string_view backend_host,
                                        std::uint16_t port,
                                        std::string_view local_lan_address);

class WindowsVmixTelestratorProbe final : public vmix::ITelestratorProbe
{
public:
  WindowsVmixTelestratorProbe(std::string host,
                              std::uint16_t port,
                              std::string username,
                              std::string password,
                              std::string local_lan_address);
  TelestratorDescriptor probe(std::stop_token stop) override;

private:
  const std::string host_;
  const std::uint16_t port_;
  const std::string username_;
  const std::string password_;
  const std::string public_url_;
};
} // namespace studio_remote::windows
