#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <studio_remote/common/types.hpp>
#include <studio_remote/credentials/device_info.hpp>
#include <studio_remote/observability/command_diagnostics.hpp>
#include <utility>
#include <vector>
struct sqlite3;

namespace studio_remote
{
struct Device
{
  Device() = default;

  Device(std::string device_id,
         std::string device_name,
         Role device_role = Role::viewer,
         std::uint64_t last_seen = 0,
         std::string provider = "obs",
         DeviceInfo info = {})
      : id(std::move(device_id)), name(std::move(device_name)), role(device_role),
        last_seen_unix_ms(last_seen), production_provider(std::move(provider)),
        device_info(std::move(info))
  {
  }

  std::string id;
  std::string name;
  Role role = Role::viewer;
  uint64_t last_seen_unix_ms = 0;
  std::string production_provider = "obs";
  DeviceInfo device_info;
  // Internal ordering within the owning AuditStore lifetime; never serialized to clients.
  std::uint64_t role_revision = 0;
  // Process-local pairing identity, replaced when a forgotten ID pairs again. Never serialized.
  std::uint64_t pairing_generation = 0;
  // Detects standalone role changes even when a later change restores the original role.
  std::uint64_t session_authorization_generation = 0;
};

struct DeviceRoleChange
{
  std::string device_id;
  Role previous_role = Role::viewer;
  Role role = Role::viewer;
  std::string production_provider = "obs";

  std::uint64_t revision = 0;

  bool operator==(const DeviceRoleChange&) const = default;
};

enum class DeviceAuthenticationFailure
{
  none,
  token_not_found,
  device_revoked
};

struct DeviceAuthenticationResult
{
  std::optional<Device> device;
  DeviceAuthenticationFailure failure = DeviceAuthenticationFailure::none;
};

enum class OperatorAssignmentPolicy
{
  transfer_existing_operators,
  allow_multiple_operators
};

class AuditStore
{
public:
  using UnixTimeProvider = std::function<std::uint64_t()>;

  explicit AuditStore(const std::filesystem::path& path,
                      UnixTimeProvider now = {},
                      EnduranceEventLog* recorder = nullptr);
  ~AuditStore();
  AuditStore(const AuditStore&) = delete;
  AuditStore& operator=(const AuditStore&) = delete;

  void add_device(const Device& device, std::string_view token_hash);
  bool has_active_operator(std::string_view production_provider);
  /// Current authorization without updating last-seen or exposing the bearer credential.
  std::optional<Role> active_device_role(std::string_view device_id);
  std::optional<Device> active_device(std::string_view device_id);
  std::optional<Device> authenticate(std::string_view token_hash);
  DeviceAuthenticationResult authenticate_with_reason(std::string_view token_hash);
  bool mark_device_seen(std::string_view device_id);
  void revoke_device(std::string_view device_id);
  bool forget_device(std::string_view device_id);
  bool rename_device(std::string_view device_id, std::string_view name);
  /// Standalone teleprompter always transfers its Operator, regardless of operator_policy.
  std::vector<DeviceRoleChange> set_role(std::string_view device_id,
                                         Role role,
                                         OperatorAssignmentPolicy operator_policy =
                                             OperatorAssignmentPolicy::transfer_existing_operators);
  std::vector<DeviceRoleChange> enforce_single_operator();
  void on_roles_changed(std::function<void(const std::vector<DeviceRoleChange>&)> handler);
  std::vector<Device> devices();
  void command(std::string_view device_id,
               std::string_view action,
               std::string_view target,
               std::string_view result,
               std::string_view obs_request_id) noexcept;
  json recent_commands(size_t limit = 100);
  std::uint64_t dropped_command_diagnostics() const noexcept;

private:
  struct DatabaseCloser
  {
    void operator()(sqlite3* database) const noexcept;
  };
  std::unique_ptr<sqlite3, DatabaseCloser> db_;
  struct CachedDevice
  {
    Device device;
    std::string token_hash;
    std::uint64_t created_at = 0;
    bool revoked = false;
  };
  using DeviceCache = std::map<std::string, CachedDevice, std::less<>>;
  using TokenIndex = std::map<std::string, std::string, std::less<>>;
  // Mutations serialize storage first, then publish under mutex_. Runtime readers only take mutex_.
  std::mutex mutation_mutex_;
  std::mutex mutex_;
  DeviceCache device_cache_;
  TokenIndex token_index_;
  std::uint64_t pairing_generation_ = 0;
  std::mutex role_change_handler_mutex_;
  std::function<void(const std::vector<DeviceRoleChange>&)> role_change_handler_;
  UnixTimeProvider now_;
  CommandDiagnostics command_diagnostics_;
  std::uint64_t role_revision_ = 0;

  // Caller holds mutation_mutex_; stamping additionally holds mutex_ after successful commit.
  void load_devices();
  std::vector<std::string> cached_operator_ids(std::string_view provider) const;
  void prepare_role_changes(const std::vector<DeviceRoleChange>& changes);
  void stamp_role_changes(std::vector<DeviceRoleChange>& changes);
  void publish_role_changes(const std::vector<DeviceRoleChange>& changes);
};
} // namespace studio_remote
