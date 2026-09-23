#include "sqlite_teleprompter_repository.hpp"
#include "../storage/sqlite_support.hpp"

#include <studio_remote/teleprompter/memory_session_repository.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace studio_remote::teleprompter
{
namespace
{
using storage::Statement;
using storage::Transaction;

template <typename Value> Result<Value> unavailable()
{
  return {
      {}, Error::storage_unavailable, "Teleprompter storage is unavailable; retry the operation"};
}

template <typename Value> Result<Value> missing()
{
  return {{}, Error::not_found, "Teleprompter script was not found"};
}

// Attachments recheck pairing inside the durable transaction as well as the memory snapshot.
// Revocation must not leave a newly inserted grant after its cleanup trigger ran.
bool active_device(sqlite3* database, std::string_view device_id)
{
  Statement statement(database, "SELECT 1 FROM devices WHERE id=? AND revoked=0");
  statement.bind(1, device_id);
  return statement.row();
}

Summary read_summary(const Statement& statement)
{
  return {statement.text(0),
          statement.text(1),
          statement.number(2),
          statement.number(3),
          statement.number(4),
          statement.number(5),
          statement.number(6)};
}

std::shared_ptr<LoadedScript> loaded_script(Script script)
{
  auto offsets = spoken_word_offsets(script.document.spoken_text);
  return std::make_shared<LoadedScript>(LoadedScript{{}, std::move(script), std::move(offsets)});
}

void write_script(sqlite3* database, const LoadedScript& loaded)
{
  const auto& script = loaded.script;
  Statement statement(database,
                      "INSERT INTO teleprompter_scripts(id,name,content,speed,revision,"
                      "created_ms,modified_ms,word_count,duration_ms) VALUES(?,?,?,?,?,?,?,?,?)");
  statement.bind(1, std::string_view(script.summary.id));
  statement.bind(2, std::string_view(script.document.draft.name));
  statement.bind(3, std::string_view(script.document.draft.content));
  statement.bind(4, static_cast<std::uint64_t>(script.document.draft.words_per_minute));
  statement.bind(5, script.summary.revision);
  statement.bind(6, script.summary.created_at_unix_ms);
  statement.bind(7, script.summary.modified_at_unix_ms);
  statement.bind(8, script.document.word_count);
  statement.bind(9, script.document.estimated_duration_ms);
  statement.finish();
}

void update_script(sqlite3* database, const LoadedScript& loaded, std::uint64_t expected_revision)
{
  const auto& script = loaded.script;
  Statement statement(database,
                      "UPDATE teleprompter_scripts SET name=?,content=?,speed=?,revision=?,"
                      "modified_ms=?,word_count=?,duration_ms=? WHERE id=? "
                      "AND revision=?");
  statement.bind(1, std::string_view(script.document.draft.name));
  statement.bind(2, std::string_view(script.document.draft.content));
  statement.bind(3, static_cast<std::uint64_t>(script.document.draft.words_per_minute));
  statement.bind(4, script.summary.revision);
  statement.bind(5, script.summary.modified_at_unix_ms);
  statement.bind(6, script.document.word_count);
  statement.bind(7, script.document.estimated_duration_ms);
  statement.bind(8, std::string_view(script.summary.id));
  statement.bind(9, expected_revision);
  statement.finish();
  if (sqlite3_changes(database) != 1)
    throw std::runtime_error("Authored teleprompter revision changed outside its owner");
}
} // namespace

void SqliteRepository::DatabaseCloser::operator()(sqlite3* database) const noexcept
{
  static_cast<void>(sqlite3_close_v2(database));
}

SqliteRepository::SqliteRepository(const std::filesystem::path& database_path,
                                   SessionDeviceSource devices,
                                   std::function<std::string()> new_id,
                                   std::function<std::uint64_t()> unix_ms,
                                   std::function<std::uint64_t()> monotonic_ms)
    : devices_(std::move(devices)), new_id_(std::move(new_id)), unix_ms_(std::move(unix_ms))
{
  if (!devices_ || !new_id_ || !unix_ms_)
    throw std::invalid_argument("Teleprompter requires device, ID and clock providers");
  const auto devices_snapshot = devices_();
  const auto path = database_path.u8string();
  sqlite3* opened = nullptr;
  const auto status = sqlite3_open_v2(reinterpret_cast<const char*>(path.c_str()),
                                      &opened,
                                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                                      nullptr);
  database_.reset(opened);
  storage::checked(status);
  storage::checked(sqlite3_busy_timeout(database_.get(), 50));
  Transaction transaction(database_.get());
  storage::execute(
      database_.get(),
      "CREATE TABLE IF NOT EXISTS teleprompter_schema(version INTEGER NOT NULL CHECK(version=3));"
      "INSERT INTO teleprompter_schema SELECT 3 WHERE NOT EXISTS(SELECT 1 FROM "
      "teleprompter_schema);");
  // Reject incompatible layouts before preparing any version-specific table or trigger SQL.
  Statement version(database_.get(), "SELECT version FROM teleprompter_schema");
  if (!version.row() || version.number(0) != 3 || version.row())
    throw std::runtime_error(
        "Unsupported teleprompter storage schema; use a fresh development data directory");
  storage::execute(
      database_.get(),
      "CREATE TABLE IF NOT EXISTS teleprompter_scripts("
      "id TEXT PRIMARY KEY,name TEXT NOT NULL,content TEXT NOT NULL,"
      "speed INTEGER NOT NULL,revision INTEGER NOT NULL,created_ms INTEGER NOT NULL,"
      "modified_ms INTEGER NOT NULL,word_count INTEGER NOT NULL,duration_ms INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS teleprompter_assignments("
      "script_id TEXT NOT NULL,device_id TEXT NOT NULL,PRIMARY KEY(script_id,device_id));"
      "CREATE TRIGGER IF NOT EXISTS teleprompter_forget AFTER DELETE ON devices BEGIN "
      "DELETE FROM teleprompter_assignments WHERE device_id=OLD.id; END;"
      "CREATE TRIGGER IF NOT EXISTS teleprompter_revoke AFTER UPDATE OF revoked ON devices "
      "WHEN NEW.revoked<>0 BEGIN DELETE FROM teleprompter_assignments WHERE device_id=NEW.id; END;"
      "DELETE FROM teleprompter_assignments WHERE NOT EXISTS "
      "(SELECT 1 FROM devices WHERE devices.id=teleprompter_assignments.device_id AND revoked=0)");
  transaction.commit();
  hydrate(devices_snapshot);
  sessions_ = std::make_unique<MemorySessionRepository>(
      devices_, [this] { return catalog_.snapshot(); }, new_id_, std::move(monotonic_ms));
}

void SqliteRepository::hydrate(const SessionDevices& devices)
{
  Statement statement(database_.get(),
                      "SELECT id,name,revision,created_ms,modified_ms,word_count,duration_ms,"
                      "content,speed,length(CAST(content AS BLOB)) "
                      "FROM teleprompter_scripts");
  while (statement.row())
  {
    const auto content_bytes = statement.number(9);
    if (content_bytes > maximum_content_bytes ||
        !catalog_.capacity_available(0, content_bytes, true))
      throw std::runtime_error("Stored teleprompter script library exceeds its capacity");
    const auto speed = statement.number(8);
    if (speed < 40 || speed > 400)
      throw std::runtime_error("Invalid stored teleprompter speed");
    auto summary = read_summary(statement);
    auto compiled =
        compile_script({summary.name, statement.text(7), static_cast<std::uint32_t>(speed)});
    if (!compiled)
      throw std::runtime_error("Invalid stored teleprompter script");
    auto loaded = loaded_script(Script{std::move(summary), std::move(*compiled.value), {}});
    Statement assignments(database_.get(),
                          "SELECT device_id FROM teleprompter_assignments WHERE script_id=?");
    assignments.bind(1, std::string_view(loaded->script.summary.id));
    while (assignments.row())
    {
      const auto device = devices.find(assignments.text(0));
      if (device != devices.end())
        loaded->assignments.emplace(device->first,
                                    ScriptAttachment{device->second.pairing_generation, new_id_()});
    }
    catalog_.publish(catalog_.prepare(std::move(loaded)));
  }
}

SqliteRepository::~SqliteRepository() = default;

ISessionRepository* SqliteRepository::shared_sessions() noexcept
{
  return sessions_.get();
}

SessionDevices SqliteRepository::current_devices()
{
  return devices_();
}

Result<std::vector<Summary>> SqliteRepository::library()
try
{
  return catalog_.library();
}
catch (const std::exception&)
{
  return unavailable<std::vector<Summary>>();
}

Result<Script> SqliteRepository::library_script(std::string_view script_id)
try
{
  return catalog_.library_script(script_id);
}
catch (const std::exception&)
{
  return unavailable<Script>();
}

Result<bool> SqliteRepository::attach(std::string_view device_id, std::string_view script_id)
try
{
  const auto grant_id = new_id_();
  if (grant_id.empty())
    return unavailable<bool>();
  const auto devices = current_devices();
  const auto device = devices.find(device_id);
  if (device == devices.end())
    return {{}, Error::device_unavailable, "Client pairing is no longer available"};
  std::lock_guard lock(storage_mutex_);
  const auto scripts = catalog_.snapshot();
  const auto found = scripts->find(script_id);
  if (found == scripts->end())
    return missing<bool>();
  Transaction transaction(database_.get());
  if (!active_device(database_.get(), device_id))
    return {{}, Error::device_unavailable, "Client pairing is no longer available"};
  if (found->second->assigned_to(device->second))
    return {true, Error::none, {}};
  if (catalog_.list(device->second).value->size() >= maximum_scripts_per_device)
    return {{}, Error::limit_reached, "Client script attachment capacity reached"};
  auto loaded = std::make_shared<LoadedScript>(*found->second);
  loaded->assignments.insert_or_assign(
      device->first, ScriptAttachment{device->second.pairing_generation, grant_id});
  auto publication = catalog_.prepare(std::move(loaded));
  Statement statement(
      database_.get(),
      "INSERT OR REPLACE INTO teleprompter_assignments(script_id,device_id) VALUES(?,?)");
  statement.bind(1, script_id);
  statement.bind(2, device_id);
  statement.finish();
  transaction.commit();
  catalog_.publish(std::move(publication));
  return {true, Error::none, {}};
}
catch (const std::exception&)
{
  return unavailable<bool>();
}

Result<bool> SqliteRepository::detach(std::string_view device_id, std::string_view script_id)
try
{
  const auto devices = current_devices();
  if (!devices.contains(device_id))
    return {{}, Error::device_unavailable, "Client pairing is no longer available"};
  std::lock_guard lock(storage_mutex_);
  const auto scripts = catalog_.snapshot();
  const auto found = scripts->find(script_id);
  if (found == scripts->end())
    return missing<bool>();
  auto loaded = std::make_shared<LoadedScript>(*found->second);
  loaded->assignments.erase(std::string(device_id));
  auto publication = catalog_.prepare(std::move(loaded));
  Transaction transaction(database_.get());
  Statement statement(database_.get(),
                      "DELETE FROM teleprompter_assignments WHERE script_id=? AND device_id=?");
  statement.bind(1, script_id);
  statement.bind(2, device_id);
  statement.finish();
  transaction.commit();
  catalog_.publish(std::move(publication));
  return {true, Error::none, {}};
}
catch (const std::exception&)
{
  return unavailable<bool>();
}

Result<std::vector<Summary>> SqliteRepository::list(std::string_view device_id)
try
{
  const auto devices = current_devices();
  const auto device = devices.find(device_id);
  if (device == devices.end())
    return {{}, Error::device_unavailable, "Client pairing is no longer available"};
  return catalog_.list(device->second);
}
catch (const std::exception&)
{
  return unavailable<std::vector<Summary>>();
}

Result<Script> SqliteRepository::get(std::string_view device_id, std::string_view script_id)
try
{
  const auto devices = current_devices();
  const auto device = devices.find(device_id);
  return device == devices.end() ? missing<Script>() : catalog_.get(device->second, script_id);
}
catch (const std::exception&)
{
  return unavailable<Script>();
}

Result<Script> SqliteRepository::retrieve(std::string_view device_id, std::string_view script_id)
try
{
  const auto devices = current_devices();
  const auto now = unix_ms_();
  const auto device = devices.find(device_id);
  return device == devices.end() ? missing<Script>()
                                 : catalog_.retrieve(device->second, script_id, now);
}
catch (const std::exception&)
{
  return unavailable<Script>();
}

Result<Script> SqliteRepository::create(Draft draft)
try
{
  auto compiled = compile_script(std::move(draft));
  if (!compiled)
    return {{}, compiled.error, std::move(compiled.message)};
  const auto id = new_id_();
  const auto now = unix_ms_();
  if (id.empty() || id.size() > 128 || now > maximum_safe_integer)
    return unavailable<Script>();
  const auto& document = *compiled.value;
  Summary summary{
      id, document.draft.name, 1, now, now, document.word_count, document.estimated_duration_ms};
  auto loaded = loaded_script(Script{std::move(summary), std::move(*compiled.value), {}});
  std::lock_guard lock(storage_mutex_);
  Transaction transaction(database_.get());
  if (!catalog_.capacity_available(0, loaded->script.document.draft.content.size(), true))
    return {{}, Error::limit_reached, "Teleprompter script library capacity reached"};
  auto publication = catalog_.prepare(loaded);
  write_script(database_.get(), *loaded);
  transaction.commit();
  catalog_.publish(std::move(publication));
  return catalog_.library_script(id);
}
catch (const std::exception&)
{
  return unavailable<Script>();
}

Result<Script>
SqliteRepository::update(std::string_view script_id, std::uint64_t expected_revision, Draft draft)
try
{
  auto compiled = compile_script(std::move(draft));
  if (!compiled)
    return {{}, compiled.error, std::move(compiled.message)};
  const auto now = unix_ms_();
  if (now > maximum_safe_integer)
    return unavailable<Script>();
  std::lock_guard lock(storage_mutex_);
  const auto snapshot = catalog_.snapshot();
  const auto found = snapshot->find(script_id);
  if (found == snapshot->end())
    return missing<Script>();
  const auto& previous = found->second;
  const auto& old = previous->script;
  if (old.summary.revision != expected_revision || expected_revision >= maximum_safe_integer)
    return {{}, Error::conflict, "Script changed; reload it before saving"};
  if (old.document.draft == compiled.value->draft)
    return catalog_.library_script(script_id);
  if (!catalog_.capacity_available(
          old.document.draft.content.size(), compiled.value->draft.content.size(), false))
    return {{}, Error::limit_reached, "Teleprompter script library capacity reached"};
  auto summary = old.summary;
  summary.name = compiled.value->draft.name;
  summary.revision++;
  summary.modified_at_unix_ms = std::max(now, summary.modified_at_unix_ms);
  summary.word_count = compiled.value->word_count;
  summary.estimated_duration_ms = compiled.value->estimated_duration_ms;
  auto loaded = loaded_script(Script{std::move(summary), std::move(*compiled.value), {}});
  loaded->assignments = previous->assignments;
  Transaction transaction(database_.get());
  auto publication = catalog_.prepare(loaded);
  update_script(database_.get(), *loaded, expected_revision);
  transaction.commit();
  catalog_.publish(std::move(publication));
  return catalog_.library_script(script_id);
}
catch (const std::exception&)
{
  return unavailable<Script>();
}

Result<bool> SqliteRepository::erase(std::string_view script_id, std::uint64_t expected_revision)
try
{
  std::lock_guard lock(storage_mutex_);
  const auto script = catalog_.library_script(script_id);
  if (!script)
    return missing<bool>();
  if (script.value->summary.revision != expected_revision)
    return {{}, Error::conflict, "Script changed; reload it before deleting"};
  Transaction transaction(database_.get());
  auto publication = catalog_.prepare_erase(script_id);
  Statement statement(database_.get(),
                      "DELETE FROM teleprompter_scripts WHERE id=? AND revision=?");
  statement.bind(1, script_id);
  statement.bind(2, expected_revision);
  statement.finish();
  if (sqlite3_changes(database_.get()) != 1)
    return missing<bool>();
  Statement assignments(database_.get(), "DELETE FROM teleprompter_assignments WHERE script_id=?");
  assignments.bind(1, script_id);
  assignments.finish();
  transaction.commit();
  catalog_.publish(std::move(publication));
  return {true, Error::none, {}};
}
catch (const std::exception&)
{
  return unavailable<bool>();
}

Result<Delivery> SqliteRepository::report_progress(std::string_view device_id,
                                                   std::string_view script_id,
                                                   const Progress& progress)
try
{
  const auto devices = current_devices();
  const auto now = unix_ms_();
  const auto device = devices.find(device_id);
  return device == devices.end()
             ? missing<Delivery>()
             : catalog_.report_progress(device->second, script_id, progress, now);
}
catch (const std::exception&)
{
  return unavailable<Delivery>();
}
} // namespace studio_remote::teleprompter
