#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <studio_remote/provider/telestrator.hpp>
#include <thread>

namespace studio_remote::vmix
{
class ITelestratorProbe
{
public:
  virtual ~ITelestratorProbe() = default;
  /// Read-only, at most four seconds including all network phases; honors cancellation.
  virtual TelestratorDescriptor probe(std::stop_token stop) = 0;
};

TelestratorDescriptor
classify_telestrator_page(unsigned status, std::string_view page, std::string_view public_url);

class VmixTelestratorDiscovery final : public ITelestratorDiscovery
{
public:
  using Now = std::function<std::chrono::steady_clock::time_point()>;
  explicit VmixTelestratorDiscovery(std::unique_ptr<ITelestratorProbe> probe,
                                    Now now = std::chrono::steady_clock::now);
  ~VmixTelestratorDiscovery() override;
  // Lifecycle calls are serialized by the adapter controller/management command owner.
  void start();
  void stop();
  void reconfigure(std::unique_ptr<ITelestratorProbe> probe);
  TelestratorDescriptor snapshot() const override;
  void on_changed(std::function<void()> callback) override;

private:
  void run(std::stop_token stop);
  void publish(TelestratorDescriptor descriptor);

  std::unique_ptr<ITelestratorProbe> probe_;
  const Now now_;
  // Worker owns probing; readers and callback registration share only this short state lock.
  mutable std::mutex mutex_;
  std::condition_variable_any wake_;
  TelestratorDescriptor descriptor_;
  std::optional<std::chrono::steady_clock::time_point> observed_;
  std::function<void()> changed_;
  std::jthread worker_;
};
} // namespace studio_remote::vmix
