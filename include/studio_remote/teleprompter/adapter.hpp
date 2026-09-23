#pragma once

#include <studio_remote/obs/obs_adapter.hpp>

#include <mutex>

namespace studio_remote::teleprompter
{
/// Local provider with no production transport, media resources or background workers.
/// GatewayRuntime serializes lifecycle calls; callbacks run on that calling thread.
class Adapter final : public IObsAdapter
{
public:
  void start() override;
  void stop() override;
  bool connected() const override;
  json request(std::string_view type,
               json data = {},
               std::source_location caller = std::source_location::current(),
               std::string_view reason = {}) override;
  void on_event(EventCallback callback) override;
  void on_connection(ConnectionCallback callback) override;
  ProductionBackendInfo backend_info() const override;

private:
  void set_running(bool running);

  mutable std::mutex mutex_;
  bool running_ = false;
  ConnectionCallback connection_callback_;
};
} // namespace studio_remote::teleprompter
