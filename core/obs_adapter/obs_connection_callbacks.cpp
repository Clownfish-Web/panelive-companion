#include <studio_remote/obs/obs_connection_callbacks.hpp>

namespace studio_remote
{
void ObsConnectionCallbacks::on_event(IObsAdapter::EventCallback callback)
{
  std::lock_guard lock(mutex_);
  // The replaced callback is destroyed with the parameter, after this lock releases.
  event_callback_.swap(callback);
}

void ObsConnectionCallbacks::on_connection(IObsAdapter::ConnectionCallback callback)
{
  std::lock_guard lock(mutex_);
  connection_callback_.swap(callback);
}

std::optional<std::string_view>
ObsConnectionCallbacks::event_received(const NormalizedObsEvent& event) const
{
  IObsAdapter::EventCallback callback;
  {
    std::lock_guard lock(mutex_);
    callback = event_callback_;
  }
  if (!callback)
    return {};
  try
  {
    callback(event);
    return {};
  }
  catch (...)
  {
    return "OBS event callback failed";
  }
}

std::optional<std::string_view>
ObsConnectionCallbacks::connection_changed(bool connected, std::string_view version) const
{
  IObsAdapter::ConnectionCallback callback;
  {
    std::lock_guard lock(mutex_);
    callback = connection_callback_;
  }
  if (!callback)
    return {};
  try
  {
    callback(connected, version);
    return {};
  }
  catch (...)
  {
    return "OBS connection callback failed";
  }
}
} // namespace studio_remote
