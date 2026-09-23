#pragma once

#include "linux_companion_services.hpp"

#include <cstdint>
#include <studio_remote/ui/management_controller.hpp>

namespace studio_remote::linux
{
std::uint64_t unix_seconds();

/// Pushes the persisted audio mix configuration into the monitor reader and canonical state.
void apply_audio_mix_configuration(const GatewayConfig& config,
                                   LinuxAudioMonitor& audio_monitor,
                                   GatewayRuntime& runtime);

/// Builds the typed callbacks the GTK management frontend invokes. Every callback runs on the UI
/// thread and performs only short, bounded work against the referenced services; listener
/// reconfiguration is the one blocking operation and is confined to the Network page action.
ManagementUiCallbacks make_management_callbacks(const LinuxCompanionServices& services);
} // namespace studio_remote::linux
