#pragma once

#import "ManagementDiagnostics.h"

#include <studio_remote/ui/management_view.hpp>

namespace studio_remote::macos
{
PLDiagnostics* diagnostics(const ManagementEnduranceStatus& endurance,
                           const std::vector<ManagementAudioOutput>& outputs,
                           const ManagementAudioDiagnosticStatus& audio);
ManagementAudioDiagnosticTap portable_tap(PLDiagnosticTap tap);
} // namespace studio_remote::macos
