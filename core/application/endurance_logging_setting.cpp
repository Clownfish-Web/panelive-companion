#include <studio_remote/observability/endurance_event_log.hpp>
#include <studio_remote/observability/endurance_logging_setting.hpp>

#include <stdexcept>

namespace studio_remote
{
void apply_endurance_logging_setting(EnduranceEventLog* logger, bool enabled)
{
  if (!logger)
    throw std::runtime_error("Debug recorder is unavailable");
  if (enabled)
    logger->enable();
  logger->record("configuration", enabled ? "debugRecordingEnabled" : "debugRecordingDisabled");
  if (!enabled)
    logger->disable();
}
} // namespace studio_remote
