#include <studio_remote/vmix/vmix_master_mute.hpp>

#include <cmath>

namespace studio_remote::vmix
{
std::optional<double> VmixMasterMute::request(bool mute, double observed_volume_db)
{
  if (mute)
  {
    if (muted(observed_volume_db) && !pending_restore_db_)
      return std::nullopt;
    // A retry must never replace the saved audible gain with the silence floor.
    if (!restore_volume_db_ || observed_volume_db > silence_db)
      restore_volume_db_ = observed_volume_db;
    pending_restore_db_.reset();
    phase_ = Phase::muting;
    return silence_db;
  }
  if (!restore_volume_db_)
    return std::nullopt;
  if (observed_volume_db > silence_db)
  {
    // An external fader edit takes precedence over a historical restore value.
    reset();
    return std::nullopt;
  }
  phase_ = Phase::unmuting;
  pending_restore_db_ = restore_volume_db_;
  return pending_restore_db_;
}

void VmixMasterMute::observe(double volume_db)
{
  if (phase_ == Phase::muting && volume_db <= silence_db)
    phase_ = Phase::muted;
  else if ((phase_ == Phase::muted && volume_db > silence_db) ||
           (pending_restore_db_ &&
            (volume_db > silence_db || std::abs(volume_db - *pending_restore_db_) <= 0.001)))
    reset();
}

bool VmixMasterMute::muted(double volume_db) const noexcept
{
  return restore_volume_db_.has_value() && volume_db <= silence_db;
}

void VmixMasterMute::volume_requested(double volume_db)
{
  if (restore_volume_db_)
  {
    phase_ = Phase::unmuting;
    pending_restore_db_ = volume_db;
  }
}

void VmixMasterMute::reset() noexcept
{
  phase_ = Phase::idle;
  restore_volume_db_.reset();
  pending_restore_db_.reset();
}
} // namespace studio_remote::vmix
