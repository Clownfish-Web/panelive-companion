#pragma once

#include <studio_remote/observability/endurance_event_log.hpp>

namespace studio_remote
{
/// The composition owns this after every dependency borrowed by the provider. Reset/destruction
/// clears the registration and joins any active sample before those dependencies can disappear.
/// The recorder outlives this owner; one composition exclusively owns its provider registration.
class EnduranceSnapshotRegistration final
{
public:
  EnduranceSnapshotRegistration(EnduranceEventLog& log,
                                EnduranceEventLog::SnapshotProvider provider);
  ~EnduranceSnapshotRegistration();
  EnduranceSnapshotRegistration(const EnduranceSnapshotRegistration&) = delete;
  EnduranceSnapshotRegistration& operator=(const EnduranceSnapshotRegistration&) = delete;

  void reset();

private:
  EnduranceEventLog* log_;
};
} // namespace studio_remote
