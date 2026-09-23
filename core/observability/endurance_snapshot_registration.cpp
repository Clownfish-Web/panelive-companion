#include <studio_remote/observability/endurance_snapshot_registration.hpp>

#include <utility>

namespace studio_remote
{
EnduranceSnapshotRegistration::EnduranceSnapshotRegistration(
    EnduranceEventLog& log, EnduranceEventLog::SnapshotProvider provider)
    : log_(&log)
{
  log_->set_snapshot_provider(std::move(provider));
}

EnduranceSnapshotRegistration::~EnduranceSnapshotRegistration()
{
  reset();
}

void EnduranceSnapshotRegistration::reset()
{
  if (!log_)
    return;
  log_->set_snapshot_provider({});
  log_ = nullptr;
}
} // namespace studio_remote
