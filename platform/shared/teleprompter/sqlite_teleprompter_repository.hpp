#pragma once

#include <studio_remote/teleprompter/repository.hpp>
#include <studio_remote/teleprompter/script_catalog.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>

struct sqlite3;

namespace studio_remote::teleprompter
{
class MemorySessionRepository;
/// Durable authored library with an in-memory compiled catalog and process-local playback state.
/// storage_mutex_ serializes authored SQL commit plus cache publication only. Hot reads and
/// telemetry never acquire it. Device/clock/ID callbacks run before either storage or catalog
/// locks.
class SqliteRepository final : public IRepository
{
public:
  SqliteRepository(const std::filesystem::path& database_path,
                   SessionDeviceSource devices,
                   std::function<std::string()> new_id,
                   std::function<std::uint64_t()> unix_ms,
                   std::function<std::uint64_t()> monotonic_ms);
  ~SqliteRepository() override;
  ISessionRepository* shared_sessions() noexcept override;

  Result<std::vector<Summary>> library() override;
  Result<Script> library_script(std::string_view script_id) override;
  Result<bool> attach(std::string_view device_id, std::string_view script_id) override;
  Result<bool> detach(std::string_view device_id, std::string_view script_id) override;
  Result<std::vector<Summary>> list(std::string_view device_id) override;
  Result<Script> get(std::string_view device_id, std::string_view script_id) override;
  Result<Script> retrieve(std::string_view device_id, std::string_view script_id) override;
  Result<Script> create(Draft draft) override;
  Result<Script>
  update(std::string_view script_id, std::uint64_t expected_revision, Draft draft) override;
  Result<bool> erase(std::string_view script_id, std::uint64_t expected_revision) override;
  Result<Delivery> report_progress(std::string_view device_id,
                                   std::string_view script_id,
                                   const Progress& progress) override;

private:
  SessionDevices current_devices();
  void hydrate(const SessionDevices& devices);
  struct DatabaseCloser
  {
    void operator()(sqlite3* database) const noexcept;
  };
  std::unique_ptr<sqlite3, DatabaseCloser> database_;
  SessionDeviceSource devices_;
  std::function<std::string()> new_id_;
  std::function<std::uint64_t()> unix_ms_;
  std::mutex storage_mutex_;
  ScriptCatalog catalog_;
  std::unique_ptr<MemorySessionRepository> sessions_;
};
} // namespace studio_remote::teleprompter
