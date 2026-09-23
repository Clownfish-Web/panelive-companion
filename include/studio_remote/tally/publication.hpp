#pragma once

#include <studio_remote/tally/definition.hpp>

#include <cstdint>
#include <variant>

namespace studio_remote::tally
{
inline constexpr std::uint64_t maximum_revision = 9007199254740991ULL;
enum class PublicationStatus
{
  pending,
  provisioned,
  sync_pending,
  error,
  revoking,
  revoked
};
enum class MutationKind
{
  publish,
  rotate,
  revoke
};
enum class PublicationError
{
  invalid,
  forbidden,
  not_found,
  conflict,
  quota,
  storage_unavailable
};

struct PublicationFailure
{
  PublicationError code;
  std::string reason;
  std::uint64_t retry_after_ms{0};
};
template <typename T> using PublicationResult = std::variant<T, PublicationFailure>;

struct Mutation
{
  std::string publication_id;
  std::string operation_id;
  std::uint64_t expected_revision{0};
};
struct Publish
{
  Mutation mutation;
  std::string profile_id;
  Definition definition;
};

// Durable management state only. Live cues, socket epochs and countdowns are never persisted.
struct Publication
{
  std::string id;
  std::string device_id;
  std::string grant_id;
  std::string provider;
  std::string profile_id;
  Definition desired;
  std::optional<Definition> committed;
  std::uint64_t revision{1};
  std::uint64_t config_revision{1};
  std::optional<std::uint64_t> remote_revision;
  std::optional<std::uint64_t> remote_config_revision;
  PublicationStatus status{PublicationStatus::pending};
  std::string operation_id;
  std::string revocation_operation_id;
  std::optional<std::string> link_secret_reference;
  std::optional<std::uint64_t> last_synced_unix_ms;
};

struct Admission
{
  Publication publication;
  bool repeated{false};
};

struct PublicationWork
{
  std::string publication_id;
  std::string operation_id;
  MutationKind kind{MutationKind::publish};
  std::uint64_t revision{0};
  std::uint64_t config_revision{0};
  std::optional<std::uint64_t> remote_revision;
  std::optional<Definition> definition;
  bool operator==(const PublicationWork&) const = default;
};

// Supplied only after remote convergence and durable protected-secret storage, never from HTTP
// clients.
struct RemoteCommit
{
  std::uint64_t resource_revision{0};
  std::uint64_t config_revision{0};
  std::optional<std::string> link_secret_reference;
};

[[nodiscard]] PublicationResult<Publication> replace_definition(const Publication& previous,
                                                                const Publish& request);
[[nodiscard]] PublicationResult<Publication>
retire_or_rotate(const Publication& previous, const Mutation& request, MutationKind kind);
} // namespace studio_remote::tally
