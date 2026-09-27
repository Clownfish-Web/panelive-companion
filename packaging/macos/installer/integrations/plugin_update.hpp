#pragma once

#include <filesystem>
#include <functional>

namespace studio_remote::macos::installer
{
using BundleVerifier = std::function<void(const std::filesystem::path&)>;

/// Whole-bundle translation of Windows ChangeObsPlugin. The caller validates the destination's
/// ownership and supplies native signature/process checks. All work is synchronous on its thread.
void update_plugin(const std::filesystem::path& source,
                   const std::filesystem::path& destination,
                   const BundleVerifier& verify,
                   const std::function<void()>& require_obs_closed,
                   const std::function<void()>& prepare_directory);

/// The Windows integration plan retries once only after access denial and successful rollback.
/// authorize runs the same plan in a separate elevated process, never user-profile operations.
void run_integration(bool elevated,
                     const std::function<void()>& apply,
                     const std::function<void()>& authorize);
} // namespace studio_remote::macos::installer
