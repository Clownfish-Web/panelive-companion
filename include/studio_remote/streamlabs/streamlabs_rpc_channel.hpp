#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace studio_remote::streamlabs
{
/// Native message I/O only. The RPC owner serializes calls; implementations own their handles.
/// connect returns only after native peer verification and any required authentication.
class IStreamlabsRpcChannel
{
public:
  virtual ~IStreamlabsRpcChannel() = default;
  virtual void connect() = 0;
  virtual void disconnect() noexcept = 0;
  virtual bool connected() const noexcept = 0;
  virtual std::string desktop_version() const = 0;
  virtual void write_message(std::string_view message) = 0;
  virtual std::string read_message(std::chrono::milliseconds timeout) = 0;
};
} // namespace studio_remote::streamlabs
