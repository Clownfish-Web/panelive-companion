#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace studio_remote
{
enum class TelestratorStatus
{
  ready,
  mode_inactive,
  not_configured,
  backend_unavailable,
  permission_required
};

struct TelestratorDescriptor
{
  TelestratorStatus status = TelestratorStatus::not_configured;
  // Present only for ready. Never contains backend credentials or page bootstrap tokens.
  std::string url;
  bool operator==(const TelestratorDescriptor&) const = default;
};

constexpr std::string_view telestrator_reason(TelestratorStatus status)
{
  switch (status)
  {
  case TelestratorStatus::ready:
    return {};
  case TelestratorStatus::mode_inactive:
    return "mode-inactive";
  case TelestratorStatus::not_configured:
    return "not-configured";
  case TelestratorStatus::permission_required:
    return "permission-required";
  case TelestratorStatus::backend_unavailable:
    return "backend-unavailable";
  }
  return "backend-unavailable";
}

class ITelestratorDiscovery
{
public:
  virtual ~ITelestratorDiscovery() = default;
  /// Short, cached read only. An observation older than five seconds fails closed.
  virtual TelestratorDescriptor snapshot() const = 0;
  /// One listener; invoked outside state locks when status or the public URL changes.
  virtual void on_changed(std::function<void()> callback) = 0;
};
} // namespace studio_remote
