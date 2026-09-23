#include <studio_remote/vmix/vmix_telestrator_discovery.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace studio_remote::vmix
{
TelestratorDescriptor
classify_telestrator_page(unsigned status, std::string_view page, std::string_view public_url)
{
  if (public_url.empty())
    return {};
  if (status == 401 || status == 403)
    return {TelestratorStatus::permission_required, {}};
  if (status != 200 || page.size() > (1U << 20))
    return {TelestratorStatus::backend_unavailable, {}};
  const auto first = page.find_first_not_of(" \t\r\n");
  if (first != std::string_view::npos)
    page = page.substr(first, page.find_last_not_of(" \t\r\n") - first + 1);
  std::string normalized(page);
  std::ranges::transform(normalized,
                         normalized.begin(),
                         [](unsigned char character)
                         { return static_cast<char>(std::tolower(character)); });
  if (normalized == "telestrator is not currently enabled.")
    return {TelestratorStatus::mode_inactive, {}};
  // Observed vMix 29 bootstrap. Inspect markers only; never retain or expose auth tokens.
  if (page.find("/telestratorsocket?auth=") == std::string_view::npos ||
      page.find("/videosocket?auth=") == std::string_view::npos ||
      page.find("<video") == std::string_view::npos)
    return {TelestratorStatus::backend_unavailable, {}};
  return {TelestratorStatus::ready, std::string(public_url)};
}

VmixTelestratorDiscovery::VmixTelestratorDiscovery(std::unique_ptr<ITelestratorProbe> probe,
                                                   Now now)
    : probe_(std::move(probe)), now_(std::move(now))
{
  if (!probe_ || !now_)
    throw std::invalid_argument("Telestrator discovery requires a probe");
}

VmixTelestratorDiscovery::~VmixTelestratorDiscovery()
{
  // Listener may already have been destroyed during composition unwinding.
  on_changed({});
  stop();
}

void VmixTelestratorDiscovery::start()
{
  if (!worker_.joinable())
    worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

void VmixTelestratorDiscovery::stop()
{
  if (worker_.joinable())
  {
    worker_.request_stop();
    wake_.notify_all();
    worker_.join();
  }
  publish({TelestratorStatus::backend_unavailable, {}});
}

void VmixTelestratorDiscovery::reconfigure(std::unique_ptr<ITelestratorProbe> probe)
{
  if (!probe)
    throw std::invalid_argument("Telestrator discovery requires a probe");
  const bool running = worker_.joinable();
  stop();
  probe_ = std::move(probe);
  if (running)
    start();
}

TelestratorDescriptor VmixTelestratorDiscovery::snapshot() const
{
  const auto now = now_();
  std::lock_guard lock(mutex_);
  if (observed_ && now - *observed_ > std::chrono::seconds(5))
    return {TelestratorStatus::backend_unavailable, {}};
  return descriptor_;
}

void VmixTelestratorDiscovery::on_changed(std::function<void()> callback)
{
  std::lock_guard lock(mutex_);
  changed_ = std::move(callback);
}

void VmixTelestratorDiscovery::publish(TelestratorDescriptor descriptor)
{
  const auto now = now_();
  std::function<void()> callback;
  {
    std::lock_guard lock(mutex_);
    if (descriptor != descriptor_)
      callback = changed_;
    descriptor_ = std::move(descriptor);
    observed_ = now;
  }
  if (callback)
    callback();
}

void VmixTelestratorDiscovery::run(std::stop_token stop)
{
  try
  {
    while (!stop.stop_requested())
    {
      const auto result = probe_->probe(stop);
      if (stop.stop_requested())
        return;
      publish(result);
      std::unique_lock lock(mutex_);
      wake_.wait_for(lock, stop, std::chrono::milliseconds(500), [] { return false; });
    }
  }
  catch (...)
  {
    // Thread boundary: optional discovery must never fail the production adapter.
    std::cerr << "vMix Telestrator discovery worker failed\n";
    publish({TelestratorStatus::backend_unavailable, {}});
  }
}
} // namespace studio_remote::vmix
