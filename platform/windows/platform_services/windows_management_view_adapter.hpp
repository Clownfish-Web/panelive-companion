#pragma once

#include "windows_obs_connection.hpp"

#include "../capture/windows_audio_diagnostic_player.hpp"

#include <studio_remote/ui/management_status_rows.hpp>
#include <studio_remote/ui/management_view.hpp>

#include <vector>

namespace studio_remote::windows
{
ManagementVmixConnection make_management_vmix_connection(const VmixConnectionStatus& status);
std::vector<ManagementAudioOutput>
make_management_audio_outputs(const std::vector<AudioOutputDevice>& outputs);
ManagementAudioDiagnosticStatus make_management_audio_status(
    const AudioDiagnosticPlaybackStatus& status, bool debug_log_active, bool debug_log_available);
} // namespace studio_remote::windows
