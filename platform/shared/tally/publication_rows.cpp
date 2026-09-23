#include "publication_storage.hpp"

#include <nlohmann/json.hpp>
#include <studio_remote/tally/protocol.hpp>

namespace studio_remote::tally::persistence
{
namespace
{
using storage::Statement;
constexpr const char* columns =
    "SELECT id,device_id,grant_id,provider,profile_id,desired,committed,revision,config_revision,"
    "remote_revision,remote_config_revision,status,operation_id,revocation_operation_id,"
    "link_reference,last_synced_ms FROM remote_tally_publications ";

Definition definition(std::string_view text)
{
  if (text.size() > remote_tally_protocol::maximum_request_bytes)
    throw std::runtime_error("Stored remote tally definition exceeds its limit");
  const auto json = nlohmann::json::parse(text, nullptr, false);
  const auto parsed = remote_tally_protocol::parse_definition(json);
  const auto* value = std::get_if<Definition>(&parsed);
  if (!value)
    throw std::runtime_error("Stored remote tally definition is invalid");
  return *value;
}

Publication read(const Statement& row)
{
  Publication value;
  value.id = row.text(0);
  value.device_id = row.text(1);
  value.grant_id = row.text(2);
  value.provider = row.text(3);
  value.profile_id = row.text(4);
  value.desired = definition(row.text(5));
  if (const auto committed = row.text(6); !committed.empty())
    value.committed = definition(committed);
  value.revision = row.number(7);
  value.config_revision = row.number(8);
  value.remote_revision = row.optional_number(9);
  value.remote_config_revision = row.optional_number(10);
  const auto status = row.number(11);
  if (status > static_cast<std::uint64_t>(PublicationStatus::revoked) || value.revision == 0 ||
      value.revision > maximum_revision || value.config_revision == 0 ||
      value.config_revision > value.revision)
    throw std::runtime_error("Stored remote tally revision is invalid");
  value.status = static_cast<PublicationStatus>(status);
  value.operation_id = row.text(12);
  value.revocation_operation_id = row.text(13);
  if (const auto reference = row.text(14); !reference.empty())
    value.link_secret_reference = reference;
  value.last_synced_unix_ms = row.optional_number(15);
  return value;
}
} // namespace

std::optional<std::string> active_grant(sqlite3* database, std::string_view device)
{
  Statement row(database,
                "SELECT g.id FROM remote_tally_grants g JOIN devices d ON d.id=g.device_id "
                "WHERE d.id=? AND d.revoked=0 AND d.production_provider='obs'");
  row.bind(1, device);
  return row.row() ? std::optional(row.text(0)) : std::nullopt;
}

std::optional<Publication> load(sqlite3* database, std::string_view id)
{
  Statement row(database, (std::string(columns) + "WHERE id=?").c_str());
  row.bind(1, id);
  return row.row() ? std::optional(read(row)) : std::nullopt;
}

std::vector<Publication> owned(sqlite3* database, std::string_view grant)
{
  Statement row(
      database,
      (std::string(columns) + "WHERE grant_id=? AND status<>5 ORDER BY id LIMIT 16").c_str());
  row.bind(1, grant);
  std::vector<Publication> result;
  while (row.row())
    result.push_back(read(row));
  return result;
}

void save(sqlite3* database, const Publication& value)
{
  Statement row(database, R"sql(
INSERT INTO remote_tally_publications(id,device_id,grant_id,provider,profile_id,tally_id,desired,committed,
 revision,config_revision,remote_revision,remote_config_revision,status,operation_id,revocation_operation_id,
 link_reference,last_synced_ms) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(id) DO UPDATE SET desired=excluded.desired,committed=excluded.committed,
 revision=excluded.revision,config_revision=excluded.config_revision,remote_revision=excluded.remote_revision,
 remote_config_revision=excluded.remote_config_revision,status=excluded.status,operation_id=excluded.operation_id,
 link_reference=excluded.link_reference,last_synced_ms=excluded.last_synced_ms
)sql");
  row.bind(1, std::string_view(value.id));
  row.bind(2, std::string_view(value.device_id));
  row.bind(3, std::string_view(value.grant_id));
  row.bind(4, std::string_view(value.provider));
  row.bind(5, std::string_view(value.profile_id));
  row.bind(6, std::string_view(value.desired.id));
  row.bind(7, std::string_view(remote_tally_protocol::definition_json(value.desired).dump()));
  row.bind(8,
           value.committed
               ? std::optional(remote_tally_protocol::definition_json(*value.committed).dump())
               : std::nullopt);
  row.bind(9, value.revision);
  row.bind(10, value.config_revision);
  row.bind(11, value.remote_revision);
  row.bind(12, value.remote_config_revision);
  row.bind(13, static_cast<std::uint64_t>(value.status));
  row.bind(14, std::string_view(value.operation_id));
  row.bind(15, std::string_view(value.revocation_operation_id));
  row.bind(16, value.link_secret_reference);
  row.bind(17, value.last_synced_unix_ms);
  row.finish();
}
} // namespace studio_remote::tally::persistence
