#include "publication_storage.hpp"

#include <nlohmann/json.hpp>
#include <studio_remote/tally/protocol.hpp>

#include <algorithm>

namespace studio_remote::tally::persistence
{
using storage::Statement;

std::optional<PublicationFailure> capacity(sqlite3* database, std::string_view grant)
{
  Statement counts(database,
                   "SELECT count(*),coalesce(sum(device_id=(SELECT device_id FROM "
                   "remote_tally_grants WHERE id=?)),0) "
                   "FROM remote_tally_publications WHERE status<>5");
  counts.bind(1, grant);
  if (!counts.row())
    return unavailable();
  if (counts.number(0) >= 64 || counts.number(1) >= 16)
    return PublicationFailure{PublicationError::quota, "publication-limit", 6000};
  return std::nullopt;
}

std::optional<PublicationFailure>
mutation_budget(sqlite3* database, std::string_view grant, std::uint64_t now)
{
  constexpr std::uint64_t retention = 24 * 60 * 60 * 1000;
  Statement prune(database,
                  "DELETE FROM remote_tally_receipts WHERE created_ms<? AND NOT EXISTS "
                  "(SELECT 1 FROM remote_tally_outbox w WHERE "
                  "w.operation_id=remote_tally_receipts.operation_id)");
  prune.bind(1, now > retention ? now - retention : 0);
  prune.finish();
  Statement count(database, "SELECT count(*) FROM remote_tally_receipts");
  if (!count.row() || count.number(0) >= 4096)
    return PublicationFailure{PublicationError::quota, "operation-receipt-limit", 6000};
  Statement budget(database, "SELECT credits,budget_ms FROM remote_tally_grants WHERE id=?");
  budget.bind(1, grant);
  if (!budget.row())
    return forbidden();
  const auto previous = budget.number(1);
  const auto elapsed = now > previous ? std::min<std::uint64_t>(now - previous, 30000) : 0;
  const auto credits = std::min<std::uint64_t>(30000, budget.number(0) + elapsed);
  if (credits < 6000)
    return PublicationFailure{PublicationError::quota, "mutation-rate-limit", 6000 - credits};
  Statement update(database, "UPDATE remote_tally_grants SET credits=?,budget_ms=? WHERE id=?");
  update.bind(1, credits - 6000);
  update.bind(2, std::max(now, previous));
  update.bind(3, grant);
  update.finish();
  return std::nullopt;
}

std::string request_content(const Mutation& mutation, MutationKind kind, const Publish* publish)
{
  nlohmann::json value{{"kind", static_cast<unsigned>(kind)},
                       {"publicationId", mutation.publication_id},
                       {"expectedRevision", mutation.expected_revision}};
  if (publish)
  {
    value["profileId"] = publish->profile_id;
    value["definition"] = remote_tally_protocol::definition_json(publish->definition);
  }
  return value.dump();
}

std::optional<PublicationResult<Admission>>
retry(sqlite3* database, std::string_view grant, const Mutation& mutation, std::string_view content)
{
  Statement existing(database,
                     "SELECT grant_id,publication_id,content FROM remote_tally_receipts "
                     "WHERE operation_id=?");
  existing.bind(1, std::string_view(mutation.operation_id));
  if (!existing.row())
    return std::nullopt;
  if (existing.text(0) != grant || existing.text(1) != mutation.publication_id ||
      existing.text(2) != content)
    return PublicationFailure{PublicationError::conflict, "operation-id-conflict"};
  auto publication = load(database, mutation.publication_id);
  if (!publication || publication->grant_id != grant)
    return missing();
  return Admission{std::move(*publication), true};
}

void receipt(sqlite3* database,
             std::string_view grant,
             const Mutation& mutation,
             std::string_view content,
             std::uint64_t now)
{
  Statement row(database,
                "INSERT INTO remote_tally_receipts(operation_id,grant_id,publication_id,"
                "content,created_ms) VALUES(?,?,?,?,?)");
  row.bind(1, std::string_view(mutation.operation_id));
  row.bind(2, grant);
  row.bind(3, std::string_view(mutation.publication_id));
  row.bind(4, content);
  row.bind(5, now);
  row.finish();
}

void enqueue(sqlite3* database, const Publication& publication, MutationKind kind)
{
  if (kind == MutationKind::publish)
  {
    // Only replace the last unsent edit. A rotation or claimed request is an ordering barrier.
    Statement coalesce(database,
                       "DELETE FROM remote_tally_outbox WHERE publication_id=? "
                       "AND kind=0 AND started=0 AND revision=(SELECT max(revision) "
                       "FROM remote_tally_outbox WHERE publication_id=?)");
    coalesce.bind(1, std::string_view(publication.id));
    coalesce.bind(2, std::string_view(publication.id));
    coalesce.finish();
  }
  else if (kind == MutationKind::revoke)
  {
    Statement retire(database,
                     "DELETE FROM remote_tally_outbox WHERE publication_id=? AND started=0");
    retire.bind(1, std::string_view(publication.id));
    retire.finish();
  }
  Statement row(database,
                "INSERT INTO remote_tally_outbox(publication_id,operation_id,kind,revision,"
                "config_revision,definition) VALUES(?,?,?,?,?,?)");
  row.bind(1, std::string_view(publication.id));
  row.bind(2, std::string_view(publication.operation_id));
  row.bind(3, static_cast<std::uint64_t>(kind));
  row.bind(4, publication.revision);
  row.bind(5, publication.config_revision);
  row.bind(6,
           kind == MutationKind::publish
               ? std::optional(remote_tally_protocol::definition_json(publication.desired).dump())
               : std::nullopt);
  row.finish();
}
} // namespace studio_remote::tally::persistence
