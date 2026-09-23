#include "publication_storage.hpp"

#include <nlohmann/json.hpp>
#include <studio_remote/tally/protocol.hpp>

namespace studio_remote::tally::persistence
{
namespace
{
using storage::Statement;

PublicationWork read_work(const Statement& row)
{
  PublicationWork work;
  work.publication_id = row.text(0);
  work.operation_id = row.text(1);
  const auto kind = row.number(2);
  if (kind > static_cast<std::uint64_t>(MutationKind::revoke))
    throw std::runtime_error("Stored remote tally operation is invalid");
  work.kind = static_cast<MutationKind>(kind);
  work.revision = row.number(3);
  work.config_revision = row.number(4);
  work.remote_revision = row.optional_number(5);
  if (const auto body = row.text(6); !body.empty())
  {
    const auto parsed = remote_tally_protocol::parse_definition(nlohmann::json::parse(body));
    if (const auto* definition = std::get_if<Definition>(&parsed))
      work.definition = *definition;
    else
      throw std::runtime_error("Stored remote tally operation definition is invalid");
  }
  return work;
}

bool valid_remote(const PublicationWork& work, const RemoteCommit& remote)
{
  if (work.kind == MutationKind::revoke)
    return true;
  constexpr std::string_view prefix = "remote-tally-link-";
  return remote.resource_revision > work.remote_revision.value_or(0) &&
         remote.resource_revision <= maximum_revision &&
         remote.config_revision == work.config_revision && remote.link_secret_reference &&
         remote.link_secret_reference->starts_with(prefix) &&
         remote_tally_protocol::valid_uuid_v4(
             std::string_view(*remote.link_secret_reference).substr(prefix.size()));
}

void install_commit(Publication& publication,
                    const PublicationWork& work,
                    const RemoteCommit& remote,
                    std::uint64_t now)
{
  if (work.kind == MutationKind::revoke)
  {
    publication.status = PublicationStatus::revoked;
    publication.link_secret_reference.reset();
    publication.committed.reset();
  }
  else
  {
    publication.remote_revision = remote.resource_revision;
    publication.remote_config_revision = remote.config_revision;
    // A late successful create still supplies the ETag needed to revoke it. It never restores
    // an author's grant or makes the withdrawn publication visible to the evaluator.
    if (publication.status != PublicationStatus::revoking)
      publication.link_secret_reference = remote.link_secret_reference;
    if (work.kind == MutationKind::publish)
      publication.committed = work.definition;
  }
  publication.last_synced_unix_ms = now;
}
} // namespace

std::optional<PublicationWork> next_work(sqlite3* database)
{
  Statement row(database, R"sql(
SELECT w.publication_id,w.operation_id,w.kind,w.revision,w.config_revision,w.remote_revision,w.definition,w.started
 FROM remote_tally_outbox w WHERE NOT EXISTS
 (SELECT 1 FROM remote_tally_outbox prior WHERE prior.publication_id=w.publication_id AND prior.revision<w.revision)
 ORDER BY w.kind=2 DESC,w.revision,w.publication_id LIMIT 1
)sql");
  if (!row.row())
    return std::nullopt;
  auto work = read_work(row);
  if (row.number(7) == 0)
  {
    const auto publication = load(database, work.publication_id);
    if (!publication)
      throw std::runtime_error("Remote tally work has no publication");
    work.remote_revision = publication->remote_revision;
    Statement claim(database,
                    "UPDATE remote_tally_outbox SET started=1,remote_revision=? "
                    "WHERE publication_id=? AND operation_id=?");
    claim.bind(1, work.remote_revision);
    claim.bind(2, std::string_view(work.publication_id));
    claim.bind(3, std::string_view(work.operation_id));
    claim.finish();
  }
  return work;
}

PublicationResult<bool> acknowledge(sqlite3* database,
                                    const PublicationWork& work,
                                    const RemoteCommit& remote,
                                    std::uint64_t now)
{
  Statement row(database,
                "SELECT publication_id,operation_id,kind,revision,config_revision,remote_revision,"
                "definition FROM remote_tally_outbox WHERE publication_id=? AND operation_id=? AND "
                "started=1");
  row.bind(1, std::string_view(work.publication_id));
  row.bind(2, std::string_view(work.operation_id));
  if (!row.row() || read_work(row) != work)
    return PublicationFailure{PublicationError::conflict, "operation-no-longer-current"};
  if (!valid_remote(work, remote))
    return PublicationFailure{PublicationError::invalid, "invalid-remote-commit"};
  auto publication = load(database, work.publication_id);
  if (!publication)
    return missing();
  install_commit(*publication, work, remote, now);
  Statement erase(database,
                  "DELETE FROM remote_tally_outbox WHERE publication_id=? AND operation_id=?");
  erase.bind(1, std::string_view(work.publication_id));
  erase.bind(2, std::string_view(work.operation_id));
  erase.finish();
  Statement pending(database, "SELECT 1 FROM remote_tally_outbox WHERE publication_id=? LIMIT 1");
  pending.bind(1, std::string_view(work.publication_id));
  if (publication->status != PublicationStatus::revoking &&
      publication->status != PublicationStatus::revoked)
    publication->status =
        pending.row() ? PublicationStatus::sync_pending : PublicationStatus::provisioned;
  save(database, *publication);
  return true;
}
} // namespace studio_remote::tally::persistence
