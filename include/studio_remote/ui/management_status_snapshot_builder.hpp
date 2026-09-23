#pragma once

#include <studio_remote/ui/management_status.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>

namespace studio_remote
{
inline constexpr auto management_health_window = std::chrono::minutes(5);

/// Builds the current management status while retaining only a bounded rolling health window.
/// The composition owns this builder longer than its management callbacks. Each build samples
/// local diagnostic values only; native I/O and frontend scheduling belong to the caller.
class ManagementStatusSnapshotBuilder
{
public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;

  ManagementStatusSnapshotBuilder();
  explicit ManagementStatusSnapshotBuilder(Clock clock);
  ~ManagementStatusSnapshotBuilder();

  ManagementStatusSnapshotBuilder(const ManagementStatusSnapshotBuilder&) = delete;
  ManagementStatusSnapshotBuilder& operator=(const ManagementStatusSnapshotBuilder&) = delete;

  ManagementStatusSnapshot build(const CompanionEndpointStatus& companion,
                                 const ObsConnectionStatus& obs,
                                 std::string_view obs_version,
                                 const nlohmann::json& capture,
                                 const nlohmann::json& audio_mixer,
                                 const nlohmann::json& thumbnails,
                                 const nlohmann::json& webrtc);

  /// Samples one provider's output. Use a separate persistent builder for each provider so
  /// their rolling counter histories remain independent.
  ManagementFeedStatus build_webrtc_output(std::string_view provider_name,
                                           const nlohmann::json& webrtc);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote
