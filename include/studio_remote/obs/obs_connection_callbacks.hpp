#pragma once

#include <mutex>
#include <optional>
#include <studio_remote/obs/obs_adapter.hpp>

namespace studio_remote
{
/// Connection-owned callback registration and synchronous reader delivery. Replacement is safe
/// during delivery: each invocation owns its callback snapshot until it returns. The lifecycle
/// owner must stop/join its reader before destroying this object or callback dependencies.
/// Callbacks run outside the registration mutex and must return promptly; they may replace a
/// callback, but cannot synchronously request OBS work or stop the reader delivering them.
class ObsConnectionCallbacks final
{
public:
  void on_event(IObsAdapter::EventCallback callback);
  void on_connection(IObsAdapter::ConnectionCallback callback);

  /// Empty means delivered or no callback installed. Failures return fixed text for the owner
  /// to record; exception messages and event payloads are never copied into the error.
  std::optional<std::string_view> event_received(const NormalizedObsEvent& event) const;
  std::optional<std::string_view> connection_changed(bool connected,
                                                     std::string_view version) const;

private:
  mutable std::mutex mutex_;
  IObsAdapter::EventCallback event_callback_;
  IObsAdapter::ConnectionCallback connection_callback_;
};
} // namespace studio_remote
