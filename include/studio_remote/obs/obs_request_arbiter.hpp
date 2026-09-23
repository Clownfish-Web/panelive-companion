#pragma once

#include <studio_remote/obs/obs_adapter.hpp>

#include <condition_variable>
#include <mutex>

namespace studio_remote
{

enum class ObsRequestLane
{
  ordinary,
  critical
};

class IObsRequestExecutor
{
public:
  virtual ~IObsRequestExecutor() = default;
  virtual json request(ObsRequestLane lane,
                       std::string_view type,
                       json data = {},
                       std::source_location caller = std::source_location::current(),
                       std::string_view reason = {}) = 0;
  virtual json request_current(ObsRequestLane lane,
                               std::string_view type,
                               json data = {},
                               std::source_location caller = std::source_location::current(),
                               std::string_view reason = {}) = 0;
};

class ObsRequestArbiter final : public IObsRequestExecutor
{
public:
  explicit ObsRequestArbiter(IObsAdapter& obs);

  json request(ObsRequestLane lane,
               std::string_view type,
               json data = {},
               std::source_location caller = std::source_location::current(),
               std::string_view reason = {}) override;
  json request_current(ObsRequestLane lane,
                       std::string_view type,
                       json data = {},
                       std::source_location caller = std::source_location::current(),
                       std::string_view reason = {}) override;
  bool critical_ready() const;

private:
  json request_impl(ObsRequestLane lane,
                    std::string_view type,
                    json data,
                    std::source_location caller,
                    std::string_view reason,
                    bool current);

  IObsAdapter& obs_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  bool request_in_flight_ = false;
  std::size_t critical_ready_count_ = 0;
};

class LaneObsAdapter final : public IObsAdapter
{
public:
  LaneObsAdapter(IObsAdapter& obs, IObsRequestExecutor& arbiter, ObsRequestLane lane);

  void start() override;
  void stop() override;
  bool connected() const override;
  json request(std::string_view type,
               json data = {},
               std::source_location caller = std::source_location::current(),
               std::string_view reason = {}) override;
  json request_current(std::string_view type,
                       json data = {},
                       std::source_location caller = std::source_location::current(),
                       std::string_view reason = {}) override;
  void on_event(EventCallback callback) override;
  void on_connection(ConnectionCallback callback) override;
  ProductionBackendInfo backend_info() const override;
  std::string last_error() const override;
  bool recording_pause_available() const override;

private:
  IObsAdapter& obs_;
  IObsRequestExecutor& arbiter_;
  ObsRequestLane lane_ = ObsRequestLane::ordinary;
};

} // namespace studio_remote
