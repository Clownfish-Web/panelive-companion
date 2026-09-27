#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <sqlite3.h>
#include <stdexcept>
#include <studio_remote/credentials/audit_store.hpp>
#include <studio_remote/credentials/storage_busy.hpp>
#include <utility>

namespace studio_remote
{
namespace
{
constexpr std::string_view standalone_teleprompter_provider = "companion-teleprompter";

std::uint64_t system_unix_ms()
{
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());
}
void checked(int result, sqlite3* db)
{
  const auto primary = result & 0xff;
  if (primary == SQLITE_BUSY || primary == SQLITE_LOCKED)
    throw CredentialStorageBusy();
  if (result != SQLITE_OK && result != SQLITE_DONE && result != SQLITE_ROW)
    throw std::runtime_error(sqlite3_errmsg(db));
}

void checked_exec(sqlite3* db, const char* sql)
{
  char* error = nullptr;
  const auto result = sqlite3_exec(db, sql, nullptr, nullptr, &error);
  std::unique_ptr<char, decltype(&sqlite3_free)> owned_error(error, sqlite3_free);
  if (result == SQLITE_OK)
    return;
  checked(result, db);
}

class Statement
{
public:
  Statement(sqlite3* db, const char* sql)
  {
    sqlite3_stmt* prepared = nullptr;
    const auto result = sqlite3_prepare_v2(db, sql, -1, &prepared, nullptr);
    value_.reset(prepared);
    checked(result, db);
  }

  bool row()
  {
    const auto result = sqlite3_step(value_.get());
    checked(result, sqlite3_db_handle(value_.get()));
    return result == SQLITE_ROW;
  }

  void bind_text(int index, std::string_view value)
  {
    checked(sqlite3_bind_text64(value_.get(),
                                index,
                                value.empty() ? "" : value.data(),
                                static_cast<sqlite3_uint64>(value.size()),
                                SQLITE_TRANSIENT,
                                SQLITE_UTF8),
            sqlite3_db_handle(value_.get()));
  }

  void bind_unsigned(int index, std::uint64_t value)
  {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
      throw std::out_of_range("Credential storage integer exceeds SQLite range");
    checked(sqlite3_bind_int64(value_.get(), index, static_cast<sqlite3_int64>(value)),
            sqlite3_db_handle(value_.get()));
  }

  void bind_null(int index)
  {
    checked(sqlite3_bind_null(value_.get(), index), sqlite3_db_handle(value_.get()));
  }

  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;

  sqlite3_stmt* get() const noexcept
  {
    return value_.get();
  }

private:
  struct Finalizer
  {
    void operator()(sqlite3_stmt* statement) const noexcept
    {
      // Finalize always releases the statement. Its result describes the last evaluation,
      // whose error belongs to the calling operation, rather than a cleanup failure.
      static_cast<void>(sqlite3_finalize(statement));
    }
  };
  std::unique_ptr<sqlite3_stmt, Finalizer> value_;
};

bool table_has_column(sqlite3* db, const char* table, std::string_view column)
{
  const std::string query = "PRAGMA table_info(" + std::string(table) + ")";
  Statement statement(db, query.c_str());
  while (statement.row())
  {
    const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 1));
    if (name && column == name)
      return true;
  }
  return false;
}

DeviceInfo
stored_device_info(sqlite3_stmt* statement, int type_column, int width_column, int height_column)
{
  DeviceInfo result;
  if (const auto* type = reinterpret_cast<const char*>(sqlite3_column_text(statement, type_column)))
    result.type = parse_device_type(type).value_or(DeviceType::unknown);

  if (sqlite3_column_type(statement, width_column) != SQLITE_NULL &&
      sqlite3_column_type(statement, height_column) != SQLITE_NULL)
  {
    const auto width = sqlite3_column_int64(statement, width_column);
    const auto height = sqlite3_column_int64(statement, height_column);
    if (width > 0 && height > 0 &&
        width <= static_cast<sqlite3_int64>(maximum_device_screen_logical_pixels) &&
        height <= static_cast<sqlite3_int64>(maximum_device_screen_logical_pixels))
      result.screen_size =
          DeviceScreenSize{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
  }
  result.type = effective_device_type(result);
  return result;
}

class Transaction
{
public:
  explicit Transaction(sqlite3* db) : db_(db)
  {
    checked_exec(db_, "BEGIN IMMEDIATE");
  }

  ~Transaction()
  {
    if (!committed_)
      sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
  }

  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

  void commit()
  {
    checked_exec(db_, "COMMIT");
    committed_ = true;
  }

private:
  sqlite3* db_;
  bool committed_ = false;
};

std::vector<std::string> active_operator_ids(sqlite3* db, std::string_view production_provider)
{
  Statement statement(db,
                      "SELECT id FROM devices WHERE role='operator' AND revoked=0 "
                      "AND production_provider=? "
                      "ORDER BY last_seen_ms DESC,created_ms DESC,id DESC");
  statement.bind_text(1, production_provider);
  std::vector<std::string> result;
  while (statement.row())
    result.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0)));
  return result;
}

void update_role(sqlite3* db, std::string_view device_id, Role role)
{
  Statement statement(db, "UPDATE devices SET role=? WHERE id=? AND revoked=0");
  const auto value = role_name(role);
  statement.bind_text(1, value);
  statement.bind_text(2, device_id);
  checked(sqlite3_step(statement.get()), db);
}

void insert_device(sqlite3* db,
                   const Device& device,
                   std::string_view token_hash,
                   std::uint64_t created_at)
{
  Statement statement(
      db,
      "INSERT INTO devices(id,name,role,token_hash,created_ms,last_seen_ms,production_provider,"
      "device_type,screen_width_logical_pixels,screen_height_logical_pixels) "
      "VALUES(?,?,?,?,?,?,?,?,?,?)");
  statement.bind_text(1, device.id);
  statement.bind_text(2, device.name);
  const auto role = role_name(device.role);
  statement.bind_text(3, role);
  statement.bind_text(4, token_hash);
  statement.bind_unsigned(5, created_at);
  statement.bind_unsigned(6, created_at);
  statement.bind_text(7, device.production_provider);
  const auto device_type = device_type_name(effective_device_type(device.device_info));
  statement.bind_text(8, device_type);
  if (device.device_info.screen_size)
  {
    statement.bind_unsigned(9, device.device_info.screen_size->width_logical_pixels);
    statement.bind_unsigned(10, device.device_info.screen_size->height_logical_pixels);
  }
  else
  {
    statement.bind_null(9);
    statement.bind_null(10);
  }
  checked(sqlite3_step(statement.get()), db);
}

void enforce_standalone_operator_invariant(sqlite3* db)
{
  Transaction transaction(db);
  // Startup uses the same most-recently-active winner as configurable operator enforcement.
  // Reconciliation precedes authentication and does not mutate any other provider's policy.
  const auto operators = active_operator_ids(db, standalone_teleprompter_provider);
  for (std::size_t index = 1; index < operators.size(); ++index)
    update_role(db, operators[index], Role::viewer);
  checked_exec(db,
               "CREATE UNIQUE INDEX IF NOT EXISTS one_active_teleprompter_operator "
               "ON devices(production_provider) WHERE role='operator' AND revoked=0 "
               "AND production_provider='companion-teleprompter'");
  transaction.commit();
}
} // namespace

void AuditStore::DatabaseCloser::operator()(sqlite3* database) const noexcept
{
  if (sqlite3_close(database) != SQLITE_OK)
    std::fputs("Could not close audit database\n", stderr);
}

AuditStore::AuditStore(const std::filesystem::path& path,
                       UnixTimeProvider now,
                       EnduranceEventLog* recorder)
    : now_(now ? std::move(now) : UnixTimeProvider(system_unix_ms)),
      command_diagnostics_(now_, recorder)
{
  sqlite3* opened = nullptr;
  const auto result =
      sqlite3_open_v2(path.string().c_str(),
                      &opened,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                      nullptr);
  // Even a failed open can allocate a connection. Adopt before checking or constructing errors.
  db_.reset(opened);
  checked(result, db_.get());
  // Other repositories share this WAL database; short commits should not reject valid requests.
  checked(sqlite3_busy_timeout(db_.get(), 50), db_.get());
  const char* schema = "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;"
                       "CREATE TABLE IF NOT EXISTS devices(id TEXT PRIMARY KEY,name TEXT NOT "
                       "NULL,role TEXT NOT NULL,"
                       " token_hash TEXT NOT NULL UNIQUE,created_ms INTEGER NOT NULL,last_seen_ms "
                       "INTEGER NOT NULL,revoked INTEGER NOT NULL DEFAULT 0);";
  checked_exec(db_.get(), schema);
  // Command evidence is transient diagnostics, never persisted execution state. Retire the old
  // diagnostic table on startup rather than retaining unbounded history from a development run.
  checked_exec(db_.get(), "DROP TABLE IF EXISTS commands");
  if (!table_has_column(db_.get(), "devices", "production_provider"))
    checked_exec(db_.get(),
                 "ALTER TABLE devices ADD COLUMN production_provider TEXT NOT NULL DEFAULT "
                 "'obs'");
  if (!table_has_column(db_.get(), "devices", "device_type"))
    checked_exec(db_.get(),
                 "ALTER TABLE devices ADD COLUMN device_type TEXT NOT NULL DEFAULT 'unknown'");
  if (!table_has_column(db_.get(), "devices", "screen_width_logical_pixels"))
    checked_exec(db_.get(), "ALTER TABLE devices ADD COLUMN screen_width_logical_pixels INTEGER");
  if (!table_has_column(db_.get(), "devices", "screen_height_logical_pixels"))
    checked_exec(db_.get(), "ALTER TABLE devices ADD COLUMN screen_height_logical_pixels INTEGER");
  checked_exec(db_.get(), "DROP INDEX IF EXISTS one_active_operator");
  enforce_standalone_operator_invariant(db_.get());
  load_devices();
}

AuditStore::~AuditStore()
{
  // Preserve close-before-callback-destruction order while also owning failed construction.
  db_.reset();
}

void AuditStore::load_devices()
{
  Statement statement(
      db_.get(),
      "SELECT id,name,role,last_seen_ms,production_provider,device_type,"
      "screen_width_logical_pixels,screen_height_logical_pixels,token_hash,created_ms,revoked "
      "FROM devices ORDER BY id");
  while (statement.row())
  {
    const auto text = [&](int column)
    { return reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), column)); };
    Device device{text(0),
                  text(1),
                  parse_role(text(2)),
                  static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 3)),
                  text(4),
                  stored_device_info(statement.get(), 5, 6, 7)};
    device.pairing_generation = ++pairing_generation_;
    device.session_authorization_generation = 1;
    CachedDevice cached{device,
                        text(8),
                        static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 9)),
                        sqlite3_column_int(statement.get(), 10) != 0};
    token_index_.emplace(cached.token_hash, device.id);
    device_cache_.emplace(device.id, std::move(cached));
  }
}

std::vector<std::string> AuditStore::cached_operator_ids(std::string_view provider) const
{
  std::vector<std::string> result;
  for (const auto& [id, cached] : device_cache_)
    if (!cached.revoked && cached.device.role == Role::operator_ &&
        cached.device.production_provider == provider)
      result.push_back(id);
  std::sort(result.begin(),
            result.end(),
            [&](const auto& left, const auto& right)
            {
              const auto& a = device_cache_.at(left);
              const auto& b = device_cache_.at(right);
              if (a.device.last_seen_unix_ms != b.device.last_seen_unix_ms)
                return a.device.last_seen_unix_ms > b.device.last_seen_unix_ms;
              if (a.created_at != b.created_at)
                return a.created_at > b.created_at;
              return left > right;
            });
  return result;
}

void AuditStore::add_device(const Device& device, std::string_view token_hash)
{
  const auto created_at = now_();
  std::vector<DeviceRoleChange> changes;
  {
    std::lock_guard mutation_lock(mutation_mutex_);
    if (pairing_generation_ == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Device pairing generation exhausted");
    Device cached_device = device;
    cached_device.last_seen_unix_ms = created_at;
    cached_device.device_info.type = effective_device_type(device.device_info);
    cached_device.role_revision = 0;
    cached_device.pairing_generation = pairing_generation_ + 1;
    cached_device.session_authorization_generation = 1;
    // Allocate both nodes before storage commits. Insertion afterward only transfers ownership.
    DeviceCache prepared_devices;
    prepared_devices.emplace(device.id,
                             CachedDevice{cached_device, std::string(token_hash), created_at});
    TokenIndex prepared_tokens;
    prepared_tokens.emplace(token_hash, device.id);
    std::vector<std::string> operators;
    {
      std::lock_guard lock(mutex_);
      operators = cached_operator_ids(device.production_provider);
    }
    Transaction transaction(db_.get());
    if (device.role == Role::operator_)
    {
      for (const auto& current_operator : operators)
      {
        if (current_operator == device.id)
          continue;
        update_role(db_.get(), current_operator, Role::viewer);
        changes.push_back(
            {current_operator, Role::operator_, Role::viewer, device.production_provider});
      }
    }

    insert_device(db_.get(), device, token_hash, created_at);
    prepare_role_changes(changes);
    transaction.commit();
    std::lock_guard lock(mutex_);
    device_cache_.merge(prepared_devices);
    token_index_.merge(prepared_tokens);
    ++pairing_generation_;
    stamp_role_changes(changes);
  }
  publish_role_changes(changes);
}

bool AuditStore::has_active_operator(std::string_view provider)
{
  std::lock_guard lock(mutex_);
  return std::any_of(device_cache_.begin(),
                     device_cache_.end(),
                     [&](const auto& entry)
                     {
                       const auto& cached = entry.second;
                       return !cached.revoked && cached.device.production_provider == provider &&
                              cached.device.role == Role::operator_;
                     });
}

std::optional<Device> AuditStore::authenticate(std::string_view token_hash)
{
  return authenticate_with_reason(token_hash).device;
}

DeviceAuthenticationResult AuditStore::authenticate_with_reason(std::string_view token_hash)
{
  const auto observed_at = now_();
  std::lock_guard lock(mutex_);
  const auto token = token_index_.find(token_hash);
  if (token == token_index_.end())
    return {std::nullopt, DeviceAuthenticationFailure::token_not_found};
  auto& cached = device_cache_.at(token->second);
  if (cached.revoked)
    return {std::nullopt, DeviceAuthenticationFailure::device_revoked};
  cached.device.last_seen_unix_ms = std::max(cached.device.last_seen_unix_ms, observed_at);
  return {cached.device, DeviceAuthenticationFailure::none};
}

bool AuditStore::mark_device_seen(std::string_view id)
{
  const auto observed_at = now_();
  std::lock_guard lock(mutex_);
  const auto found = device_cache_.find(id);
  if (found == device_cache_.end() || found->second.revoked)
    return false;
  auto& last_seen = found->second.device.last_seen_unix_ms;
  last_seen = std::max(last_seen, observed_at);
  return true;
}

void AuditStore::revoke_device(std::string_view id)
{
  std::lock_guard mutation_lock(mutation_mutex_);
  Statement statement(db_.get(), "UPDATE devices SET revoked=1 WHERE id=?");
  statement.bind_text(1, id);
  checked(sqlite3_step(statement.get()), db_.get());
  std::lock_guard lock(mutex_);
  if (const auto found = device_cache_.find(id); found != device_cache_.end())
    found->second.revoked = true;
}

std::optional<Device> AuditStore::active_device(std::string_view id)
{
  std::lock_guard lock(mutex_);
  const auto found = device_cache_.find(id);
  if (found == device_cache_.end() || found->second.revoked)
    return std::nullopt;
  return found->second.device;
}

std::optional<Role> AuditStore::active_device_role(std::string_view id)
{
  const auto device = active_device(id);
  return device ? std::optional(device->role) : std::nullopt;
}

bool AuditStore::forget_device(std::string_view id)
{
  std::lock_guard mutation_lock(mutation_mutex_);
  Statement statement(db_.get(), "DELETE FROM devices WHERE id=?");
  statement.bind_text(1, id);
  checked(sqlite3_step(statement.get()), db_.get());
  const bool forgotten = sqlite3_changes(db_.get()) > 0;
  if (forgotten)
  {
    std::lock_guard lock(mutex_);
    const auto found = device_cache_.find(id);
    if (found != device_cache_.end())
    {
      token_index_.erase(found->second.token_hash);
      device_cache_.erase(found);
    }
  }
  return forgotten;
}

bool AuditStore::rename_device(std::string_view id, std::string_view name)
{
  std::lock_guard mutation_lock(mutation_mutex_);
  std::string prepared_name(name);
  Statement statement(db_.get(), "UPDATE devices SET name=? WHERE id=? AND revoked=0");
  statement.bind_text(1, name);
  statement.bind_text(2, id);
  checked(sqlite3_step(statement.get()), db_.get());
  const bool renamed = sqlite3_changes(db_.get()) > 0;
  if (renamed)
  {
    std::lock_guard lock(mutex_);
    device_cache_.find(id)->second.device.name.swap(prepared_name);
  }
  return renamed;
}

std::vector<DeviceRoleChange>
AuditStore::set_role(std::string_view id, Role role, OperatorAssignmentPolicy operator_policy)
{
  std::vector<DeviceRoleChange> changes;
  {
    std::lock_guard mutation_lock(mutation_mutex_);
    const auto current = active_device(id);
    if (!current)
      return changes;
    const auto previous_role = current->role;
    const auto& production_provider = current->production_provider;
    std::vector<std::string> operators;
    {
      std::lock_guard lock(mutex_);
      operators = cached_operator_ids(production_provider);
    }
    Transaction transaction(db_.get());

    if (role == Role::operator_ &&
        (production_provider == standalone_teleprompter_provider ||
         operator_policy == OperatorAssignmentPolicy::transfer_existing_operators))
    {
      for (const auto& current_operator : operators)
      {
        if (current_operator == id)
          continue;
        update_role(db_.get(), current_operator, Role::viewer);
        changes.push_back({current_operator, Role::operator_, Role::viewer, production_provider});
      }
    }
    if (previous_role != role)
    {
      update_role(db_.get(), id, role);
      changes.push_back({std::string(id), previous_role, role, production_provider});
    }
    prepare_role_changes(changes);
    transaction.commit();
    std::lock_guard lock(mutex_);
    stamp_role_changes(changes);
  }
  publish_role_changes(changes);
  return changes;
}

std::vector<DeviceRoleChange> AuditStore::enforce_single_operator()
{
  std::vector<DeviceRoleChange> changes;
  {
    std::lock_guard mutation_lock(mutation_mutex_);
    {
      std::lock_guard lock(mutex_);
      std::vector<std::string> providers;
      for (const auto& [id, cached] : device_cache_)
        if (!cached.revoked)
          providers.push_back(cached.device.production_provider);
      std::sort(providers.begin(), providers.end());
      providers.erase(std::unique(providers.begin(), providers.end()), providers.end());
      for (const auto& provider : providers)
      {
        const auto operators = cached_operator_ids(provider);
        for (std::size_t index = 1; index < operators.size(); ++index)
          changes.push_back({operators[index], Role::operator_, Role::viewer, provider});
      }
    }
    prepare_role_changes(changes);
    Transaction transaction(db_.get());
    for (const auto& change : changes)
      update_role(db_.get(), change.device_id, change.role);
    transaction.commit();
    std::lock_guard lock(mutex_);
    stamp_role_changes(changes);
  }
  publish_role_changes(changes);
  return changes;
}

void AuditStore::prepare_role_changes(const std::vector<DeviceRoleChange>& changes)
{
  if (changes.empty())
    return;
  if (role_revision_ == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("Device role revision exhausted");
  std::lock_guard lock(mutex_);
  for (const auto& change : changes)
    if (change.production_provider == standalone_teleprompter_provider &&
        device_cache_.at(change.device_id).device.session_authorization_generation ==
            std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Device session authorization generation exhausted");
}

void AuditStore::stamp_role_changes(std::vector<DeviceRoleChange>& changes)
{
  if (changes.empty())
    return;
  const auto revision = ++role_revision_;
  for (auto& change : changes)
  {
    auto& device = device_cache_.at(change.device_id).device;
    device.role = change.role;
    device.role_revision = revision;
    if (change.production_provider == standalone_teleprompter_provider)
      ++device.session_authorization_generation;
    change.revision = revision;
  }
}

void AuditStore::on_roles_changed(std::function<void(const std::vector<DeviceRoleChange>&)> handler)
{
  std::lock_guard lock(role_change_handler_mutex_);
  role_change_handler_ = std::move(handler);
}

void AuditStore::publish_role_changes(const std::vector<DeviceRoleChange>& changes)
{
  if (changes.empty())
    return;
  std::function<void(const std::vector<DeviceRoleChange>&)> handler;
  {
    std::lock_guard lock(role_change_handler_mutex_);
    handler = role_change_handler_;
  }
  if (handler)
    handler(changes);
}

std::vector<Device> AuditStore::devices()
{
  std::vector<Device> result;
  {
    std::lock_guard lock(mutex_);
    for (const auto& [id, cached] : device_cache_)
      if (!cached.revoked)
        result.push_back(cached.device);
  }
  std::sort(result.begin(),
            result.end(),
            [](const auto& left, const auto& right) { return left.name < right.name; });
  return result;
}

void AuditStore::command(std::string_view device_id,
                         std::string_view action,
                         std::string_view target,
                         std::string_view result,
                         std::string_view request_id) noexcept
{
  command_diagnostics_.record(device_id, action, target, result, request_id);
}

json AuditStore::recent_commands(size_t limit)
{
  return command_diagnostics_.recent(limit);
}

std::uint64_t AuditStore::dropped_command_diagnostics() const noexcept
{
  return command_diagnostics_.dropped();
}
} // namespace studio_remote
