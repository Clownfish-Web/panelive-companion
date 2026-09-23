#pragma once

namespace studio_remote
{
class EnduranceEventLog;

/// Transient debug-session action. Only the recorder worker performs storage work.
void apply_endurance_logging_setting(EnduranceEventLog* logger, bool enabled);
} // namespace studio_remote
