#include <studio_remote/observability/endurance_event_log.hpp>
#include <studio_remote/observability/endurance_process_log.hpp>

#include <stdexcept>

namespace studio_remote
{
EnduranceProcessLog::EnduranceProcessLog() = default;
EnduranceProcessLog::~EnduranceProcessLog() = default;

EnduranceEventLog& EnduranceProcessLog::start(const std::filesystem::path& directory,
                                              const std::string& version)
{
  if (log_)
    throw std::logic_error("Process endurance logger is already initialized");
  log_ = std::make_unique<EnduranceEventLog>(directory, version);
  return *log_;
}

void EnduranceProcessLog::finish()
{
  if (!log_)
    return;
  log_->record("process", "stopped");
  log_->stop();
}

void EnduranceProcessLog::fail(std::string_view message)
{
  if (!log_)
    return;
  log_->set_snapshot_provider({});
  log_->record("process", "fatalException", {{"message", message}});
  log_->stop();
}
} // namespace studio_remote
