#include <studio_remote/obs/obs_request_arbiter.hpp>

namespace studio_remote
{

ObsRequestArbiter::ObsRequestArbiter(IObsAdapter& obs) : obs_(obs)
{
}

json ObsRequestArbiter::request(ObsRequestLane lane,
                                std::string_view type,
                                json data,
                                std::source_location caller,
                                std::string_view reason)
{
  return *request_impl(lane, type, std::move(data), caller, reason, false, {});
}

json ObsRequestArbiter::request_current(ObsRequestLane lane,
                                        std::string_view type,
                                        json data,
                                        std::source_location caller,
                                        std::string_view reason)
{
  return *request_impl(lane, type, std::move(data), caller, reason, true, {});
}

std::optional<json> ObsRequestArbiter::request_authorized(ObsRequestLane lane,
                                                          std::string_view type,
                                                          json data,
                                                          const std::function<bool()>& authorized,
                                                          std::source_location caller,
                                                          std::string_view reason)
{
  if (!authorized)
    return std::nullopt;
  return request_impl(lane, type, std::move(data), caller, reason, false, authorized);
}

std::optional<json> ObsRequestArbiter::request_impl(ObsRequestLane lane,
                                                    std::string_view type,
                                                    json data,
                                                    std::source_location caller,
                                                    std::string_view reason,
                                                    bool current,
                                                    const std::function<bool()>& authorized)
{
  {
    std::unique_lock lock(mutex_);
    if (lane == ObsRequestLane::critical)
      ++critical_ready_count_;
    changed_.wait(lock,
                  [&]
                  {
                    return !request_in_flight_ &&
                           (lane == ObsRequestLane::critical || critical_ready_count_ == 0);
                  });
    if (lane == ObsRequestLane::critical)
      --critical_ready_count_;
    request_in_flight_ = true;
  }

  try
  {
    auto result =
        authorized ? obs_.request_authorized(type, std::move(data), authorized, caller, reason)
        : current ? std::optional<json>(obs_.request_current(type, std::move(data), caller, reason))
                  : std::optional<json>(obs_.request(type, std::move(data), caller, reason));
    {
      std::lock_guard lock(mutex_);
      request_in_flight_ = false;
    }
    changed_.notify_all();
    return result;
  }
  catch (...)
  {
    {
      std::lock_guard lock(mutex_);
      request_in_flight_ = false;
    }
    changed_.notify_all();
    throw;
  }
}

bool ObsRequestArbiter::critical_ready() const
{
  std::lock_guard lock(mutex_);
  return critical_ready_count_ > 0;
}

LaneObsAdapter::LaneObsAdapter(IObsAdapter& obs, IObsRequestExecutor& arbiter, ObsRequestLane lane)
    : obs_(obs), arbiter_(arbiter), lane_(lane)
{
}

void LaneObsAdapter::start()
{
  obs_.start();
}

void LaneObsAdapter::stop()
{
  obs_.stop();
}

bool LaneObsAdapter::connected() const
{
  return obs_.connected();
}

json LaneObsAdapter::request(std::string_view type,
                             json data,
                             std::source_location caller,
                             std::string_view reason)
{
  return arbiter_.request(lane_, type, std::move(data), caller, reason);
}

std::optional<json> LaneObsAdapter::request_authorized(std::string_view type,
                                                       json data,
                                                       const std::function<bool()>& authorized,
                                                       std::source_location caller,
                                                       std::string_view reason)
{
  return arbiter_.request_authorized(lane_, type, std::move(data), authorized, caller, reason);
}

json LaneObsAdapter::request_current(std::string_view type,
                                     json data,
                                     std::source_location caller,
                                     std::string_view reason)
{
  return arbiter_.request_current(lane_, type, std::move(data), caller, reason);
}

void LaneObsAdapter::on_event(EventCallback callback)
{
  obs_.on_event(std::move(callback));
}

void LaneObsAdapter::on_connection(ConnectionCallback callback)
{
  obs_.on_connection(std::move(callback));
}

ProductionBackendInfo LaneObsAdapter::backend_info() const
{
  return obs_.backend_info();
}

std::string LaneObsAdapter::last_error() const
{
  return obs_.last_error();
}

bool LaneObsAdapter::recording_pause_available() const
{
  return obs_.recording_pause_available();
}

} // namespace studio_remote
