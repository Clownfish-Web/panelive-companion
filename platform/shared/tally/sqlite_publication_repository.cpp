#include "sqlite_publication_repository.hpp"
#include "publication_storage.hpp"

#include <studio_remote/tally/protocol.hpp>

namespace studio_remote::tally
{
namespace
{
using storage::Statement;
using storage::Transaction;
namespace wire = remote_tally_protocol;
namespace db = persistence;

bool valid(const Mutation& request)
{
  return wire::valid_uuid_v4(request.publication_id) && wire::valid_uuid_v4(request.operation_id) &&
         request.expected_revision < maximum_revision;
}

std::optional<PublicationFailure>
identity_available(sqlite3* database, std::string_view grant, const Publish& request)
{
  Statement row(database,
                "SELECT 1 FROM remote_tally_publications WHERE grant_id=? AND profile_id=? "
                "AND tally_id=? AND status<>5");
  row.bind(1, grant);
  row.bind(2, std::string_view(request.profile_id));
  row.bind(3, std::string_view(request.definition.id));
  if (row.row())
    return PublicationFailure{PublicationError::conflict, "identity-conflict"};
  return db::capacity(database, grant);
}

PublicationResult<Publication> prepare_publish(sqlite3* database,
                                               std::string_view device,
                                               std::string_view grant,
                                               const Publish& request,
                                               std::string_view revocation_id)
{
  if (const auto previous = db::load(database, request.mutation.publication_id))
  {
    if (previous->grant_id != grant)
      return request.mutation.expected_revision == 0
                 ? PublicationFailure{PublicationError::conflict, "publication-id-conflict"}
                 : db::missing();
    return replace_definition(*previous, request);
  }
  if (request.mutation.expected_revision != 0)
    return PublicationFailure{PublicationError::conflict, "revision-conflict"};
  if (const auto failure = identity_available(database, grant, request))
    return *failure;
  Publication result;
  result.id = request.mutation.publication_id;
  result.device_id = device;
  result.grant_id = grant;
  result.provider = "obs"; // active_grant derives the sole supported provider from pairing.
  result.profile_id = request.profile_id;
  result.desired = request.definition;
  result.operation_id = request.mutation.operation_id;
  result.revocation_operation_id = revocation_id;
  return result;
}

bool queue_full(sqlite3* database, std::string_view id, MutationKind kind)
{
  Statement count(database, "SELECT count(*) FROM remote_tally_outbox WHERE publication_id=?");
  count.bind(1, id);
  if (!count.row())
    return true;
  if (count.number(0) < 8)
    return false;
  if (kind != MutationKind::publish)
    return true;
  Statement last(database,
                 "SELECT kind,started FROM remote_tally_outbox WHERE publication_id=? "
                 "ORDER BY revision DESC LIMIT 1");
  last.bind(1, id);
  // A replacement consumes no additional slot when enqueue can coalesce the last unsent edit.
  return !last.row() || last.number(0) != static_cast<std::uint64_t>(MutationKind::publish) ||
         last.number(1) != 0;
}
} // namespace

void SqlitePublicationRepository::Closer::operator()(sqlite3* database) const noexcept
{
  static_cast<void>(sqlite3_close_v2(database));
}

SqlitePublicationRepository::SqlitePublicationRepository(const std::filesystem::path& path,
                                                         std::function<std::string()> uuid,
                                                         std::function<std::uint64_t()> unix_ms)
    : uuid_(std::move(uuid)), unix_ms_(std::move(unix_ms))
{
  if (!uuid_ || !unix_ms_)
    throw std::invalid_argument("Remote tally requires UUID and clock providers");
  const auto encoded = path.u8string();
  sqlite3* opened = nullptr;
  const auto status = sqlite3_open_v2(reinterpret_cast<const char*>(encoded.c_str()),
                                      &opened,
                                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                                      nullptr);
  database_.reset(opened);
  storage::checked(status);
  storage::checked(sqlite3_busy_timeout(database_.get(), 50));
  // DELETE on grants must fire its retirement trigger when invoked by the devices trigger.
  storage::execute(database_.get(), "PRAGMA recursive_triggers=ON; PRAGMA synchronous=FULL;");
  db::initialize(database_.get());
}

SqlitePublicationRepository::~SqlitePublicationRepository() = default;

PublicationResult<bool> SqlitePublicationRepository::grant(std::string_view device)
try
{
  const auto id = uuid_();
  if (!wire::valid_uuid_v4(id))
    return db::unavailable();
  std::lock_guard lock(storage_mutex_);
  Transaction transaction(database_.get());
  Statement active(database_.get(),
                   "SELECT 1 FROM devices WHERE id=? AND revoked=0 AND production_provider='obs'");
  active.bind(1, device);
  if (!active.row())
    return db::forbidden();
  Statement row(database_.get(),
                "INSERT INTO remote_tally_grants(device_id,id) VALUES(?,?) "
                "ON CONFLICT(device_id) DO NOTHING");
  row.bind(1, device);
  row.bind(2, std::string_view(id));
  row.finish();
  transaction.commit();
  return true;
}
catch (const std::exception&)
{
  return db::unavailable();
}

PublicationResult<bool> SqlitePublicationRepository::withdraw(std::string_view device)
try
{
  std::lock_guard lock(storage_mutex_);
  Transaction transaction(database_.get());
  Statement row(database_.get(), "DELETE FROM remote_tally_grants WHERE device_id=?");
  row.bind(1, device);
  row.finish();
  transaction.commit();
  return true;
}
catch (const std::exception&)
{
  return db::unavailable();
}

PublicationResult<bool> SqlitePublicationRepository::granted(std::string_view device)
try
{
  std::lock_guard lock(storage_mutex_);
  return db::active_grant(database_.get(), device).has_value();
}
catch (const std::exception&)
{
  return db::unavailable();
}

PublicationResult<Admission> SqlitePublicationRepository::publish(std::string_view device,
                                                                  const Publish& request)
try
{
  if (!valid(request.mutation) || !valid_text(request.profile_id, 128) ||
      validate(request.definition))
    return PublicationFailure{PublicationError::invalid, "invalid-publication"};
  const auto content = db::request_content(request.mutation, MutationKind::publish, &request);
  if (content.size() > wire::maximum_request_bytes)
    return PublicationFailure{PublicationError::invalid, "request-too-large"};
  const auto now = unix_ms_();
  const auto revoke_id = uuid_();
  if (now > maximum_revision || !wire::valid_uuid_v4(revoke_id))
    return db::unavailable();
  std::lock_guard lock(storage_mutex_);
  Transaction transaction(database_.get());
  const auto grant = db::active_grant(database_.get(), device);
  if (!grant)
    return db::forbidden();
  if (const auto existing = db::retry(database_.get(), *grant, request.mutation, content))
    return *existing;
  auto prepared = prepare_publish(database_.get(), device, *grant, request, revoke_id);
  if (const auto* failure = std::get_if<PublicationFailure>(&prepared))
    return *failure;
  const auto previous = db::load(database_.get(), request.mutation.publication_id);
  auto publication = std::get<Publication>(std::move(prepared));
  const bool changed = !previous || previous->desired != publication.desired;
  if (changed &&
      queue_full(database_.get(), request.mutation.publication_id, MutationKind::publish))
    return PublicationFailure{PublicationError::quota, "publication-outbox-limit", 6000};
  if (const auto failure = db::mutation_budget(database_.get(), *grant, now))
    return *failure;
  db::save(database_.get(), publication);
  if (changed)
    db::enqueue(database_.get(), publication, MutationKind::publish);
  db::receipt(database_.get(), *grant, request.mutation, content, now);
  transaction.commit();
  return Admission{std::move(publication), false};
}
catch (const std::exception&)
{
  return db::unavailable();
}

PublicationResult<Admission> SqlitePublicationRepository::mutate(std::string_view device,
                                                                 const Mutation& request,
                                                                 MutationKind kind)
try
{
  if (!valid(request))
    return PublicationFailure{PublicationError::invalid, "invalid-operation"};
  const auto now = unix_ms_();
  if (now > maximum_revision)
    return db::unavailable();
  const auto content = db::request_content(request, kind);
  std::lock_guard lock(storage_mutex_);
  Transaction transaction(database_.get());
  const auto grant = db::active_grant(database_.get(), device);
  if (!grant)
    return db::forbidden();
  if (const auto existing = db::retry(database_.get(), *grant, request, content))
    return *existing;
  const auto previous = db::load(database_.get(), request.publication_id);
  if (!previous || previous->grant_id != *grant)
    return db::missing();
  auto prepared = retire_or_rotate(*previous, request, kind);
  if (const auto* failure = std::get_if<PublicationFailure>(&prepared))
    return *failure;
  if (kind != MutationKind::revoke && queue_full(database_.get(), request.publication_id, kind))
    return PublicationFailure{PublicationError::quota, "publication-outbox-limit", 6000};
  if (const auto failure = db::mutation_budget(database_.get(), *grant, now))
    return *failure;
  auto publication = std::get<Publication>(std::move(prepared));
  db::save(database_.get(), publication);
  db::enqueue(database_.get(), publication, kind);
  db::receipt(database_.get(), *grant, request, content, now);
  transaction.commit();
  return Admission{std::move(publication), false};
}
catch (const std::exception&)
{
  return db::unavailable();
}

PublicationResult<Admission> SqlitePublicationRepository::rotate(std::string_view device,
                                                                 const Mutation& request)
{
  return mutate(device, request, MutationKind::rotate);
}

PublicationResult<Admission> SqlitePublicationRepository::unpublish(std::string_view device,
                                                                    const Mutation& request)
{
  return mutate(device, request, MutationKind::revoke);
}

PublicationResult<Publication> SqlitePublicationRepository::get(std::string_view device,
                                                                std::string_view id)
try
{
  std::lock_guard lock(storage_mutex_);
  Transaction transaction(database_.get());
  const auto grant = db::active_grant(database_.get(), device);
  if (!grant)
    return db::forbidden();
  const auto value = db::load(database_.get(), id);
  if (!value || value->grant_id != *grant)
    return db::missing();
  return *value;
}
catch (const std::exception&)
{
  return db::unavailable();
}

PublicationResult<std::vector<Publication>>
SqlitePublicationRepository::list(std::string_view device)
try
{
  std::lock_guard lock(storage_mutex_);
  Transaction transaction(database_.get());
  const auto grant = db::active_grant(database_.get(), device);
  if (!grant)
    return db::forbidden();
  return db::owned(database_.get(), *grant);
}
catch (const std::exception&)
{
  return db::unavailable();
}

PublicationResult<std::optional<PublicationWork>> SqlitePublicationRepository::claim_next()
try
{
  std::lock_guard lock(storage_mutex_);
  Transaction transaction(database_.get());
  auto work = db::next_work(database_.get());
  transaction.commit();
  return work;
}
catch (const std::exception&)
{
  return db::unavailable();
}

PublicationResult<bool> SqlitePublicationRepository::acknowledge(const PublicationWork& work,
                                                                 const RemoteCommit& remote)
try
{
  const auto now = unix_ms_();
  if (now > maximum_revision)
    return db::unavailable();
  std::lock_guard lock(storage_mutex_);
  Transaction transaction(database_.get());
  auto result = db::acknowledge(database_.get(), work, remote, now);
  if (std::holds_alternative<bool>(result))
    transaction.commit();
  return result;
}
catch (const std::exception&)
{
  return db::unavailable();
}
} // namespace studio_remote::tally
