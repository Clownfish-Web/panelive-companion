#pragma once

#include <optional>

namespace studio_remote::vmix
{
/// Companion-owned Master mute policy. The adapter serializes access with its transport lock.
/// Gain is retained across receiver reconnects and shared by all clients of this adapter.
class VmixMasterMute
{
public:
  static constexpr double silence_db = -100.0;

  std::optional<double> request(bool muted, double observed_volume_db);
  void observe(double volume_db);
  bool muted(double volume_db) const noexcept;
  void volume_requested(double volume_db);
  void reset() noexcept;

private:
  enum class Phase
  {
    idle,
    muting,
    muted,
    unmuting
  };
  Phase phase_ = Phase::idle;
  std::optional<double> restore_volume_db_;
  std::optional<double> pending_restore_db_;
};
} // namespace studio_remote::vmix
