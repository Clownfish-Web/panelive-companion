#pragma once

#include <filesystem>
#include <vector>

namespace studio_remote::windows
{

/// Composes the NDI runtime directory that a vMix installation root exposes.
///
/// vMix ships the NDI runtime inside its own installation tree rather than through the NDI SDK
/// installer, so the runtime location is derived from the installation root instead of being
/// searched for independently.
std::filesystem::path vmix_ndi_runtime_directory(const std::filesystem::path& installation_root);

/// Reports every discovered local vMix installation root, most authoritative first.
///
/// vMix does not publish an `App Paths` entry, so discovery reads the installer-recorded location
/// from the Windows uninstall registry. The historical `%ProgramFiles(x86)%\vMix` default is kept
/// as the last candidate because it remains correct for a default installation and must keep
/// working when the registry is unreadable. An installation on any other volume, which the fixed
/// default can never describe, is discovered through the registry.
///
/// `configured_root` is the operator's explicit choice from the management window. It is offered
/// first and never suppresses the discovered locations, so a stale configured value degrades to
/// automatic detection instead of disabling video.
std::vector<std::filesystem::path>
discover_vmix_installation_roots(const std::filesystem::path& configured_root = {});

/// Reports the NDI runtime directories of every discovered vMix installation, in the same order.
std::vector<std::filesystem::path>
discover_vmix_ndi_runtime_directories(const std::filesystem::path& configured_root = {});

/// Stores the operator's configured installation root for later automatic lookups.
///
/// The NDI runtime is loaded from deep inside the capture path, which has no access to the gateway
/// configuration. The composition root publishes the configured value here once during startup so
/// that lookup keeps its single, documented resolution order.
void set_configured_vmix_installation_root(std::filesystem::path configured_root);

/// Reports the OBS Studio directory that the Companion installer wrote the plugin into.
///
/// The running Companion reaches OBS over WebSocket and the plugin's local IPC, so it never needs
/// this path. It is read only to show operators where the plugin was placed; moving it requires
/// reinstalling the plugin and is therefore an installer responsibility.
std::filesystem::path installed_obs_directory();

} // namespace studio_remote::windows
