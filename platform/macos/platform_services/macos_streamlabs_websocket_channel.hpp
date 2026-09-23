#pragma once

#include <functional>
#include <memory>
#include <studio_remote/streamlabs/streamlabs_rpc_channel.hpp>

namespace studio_remote::macos
{
class ObsSocketConnection;

/// Android StreamlabsWebSocketRpc's authenticated connection sequence, with the existing native
/// socket owner. The enclosing StreamlabsRpcTransport serializes calls and owns its lifetime.
class MacosStreamlabsWebSocketChannel final : public streamlabs::IStreamlabsRpcChannel
{
public:
  using VersionProbe = std::function<std::string()>;
  MacosStreamlabsWebSocketChannel(std::uint16_t port,
                                  std::string token,
                                  VersionProbe version_probe);
  ~MacosStreamlabsWebSocketChannel() override;
  void connect() override;
  void disconnect() noexcept override;
  bool connected() const noexcept override;
  std::string desktop_version() const override;
  void write_message(std::string_view message) override;
  std::string read_message(std::chrono::milliseconds timeout) override;

private:
  const std::uint16_t port_;
  const std::string token_;
  const VersionProbe version_probe_;
  std::string version_;
  std::shared_ptr<ObsSocketConnection> connection_;
};
} // namespace studio_remote::macos
