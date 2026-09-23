#pragma once

#include <studio_remote/obs/obs_adapter.hpp>
#include <studio_remote/performance/production_performance.hpp>
#include <studio_remote/state/state_store.hpp>

namespace studio_remote
{

class PerformanceSampler
{
public:
  PerformanceSampler(IObsAdapter& obs, StateStore& state);

  void sample();
  static ProductionPerformanceSample parse(const json& value);

private:
  IObsAdapter& obs_;
  StateStore& state_;
};

} // namespace studio_remote
