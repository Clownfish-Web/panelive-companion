#include "publication_storage.hpp"

namespace studio_remote::tally::persistence
{
void initialize(sqlite3* database)
{
  storage::Transaction transaction(database);
  storage::execute(
      database,
      "CREATE TABLE IF NOT EXISTS remote_tally_schema(version INTEGER NOT NULL CHECK(version=1));"
      "INSERT INTO remote_tally_schema SELECT 1 WHERE NOT EXISTS(SELECT 1 FROM "
      "remote_tally_schema);");
  storage::Statement version(database, "SELECT version FROM remote_tally_schema");
  if (!version.row() || version.number(0) != 1 || version.row())
    throw std::runtime_error("Unsupported remote tally storage schema");
  storage::execute(database, R"sql(
CREATE TABLE IF NOT EXISTS remote_tally_grants(
 device_id TEXT PRIMARY KEY, id TEXT NOT NULL UNIQUE,
 credits INTEGER NOT NULL DEFAULT 30000, budget_ms INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS remote_tally_publications(
 id TEXT PRIMARY KEY,device_id TEXT NOT NULL,grant_id TEXT NOT NULL,provider TEXT NOT NULL,
 profile_id TEXT NOT NULL,tally_id TEXT NOT NULL,desired TEXT NOT NULL CHECK(length(desired)<=65536),
 committed TEXT CHECK(length(committed)<=65536),revision INTEGER NOT NULL,config_revision INTEGER NOT NULL,
 remote_revision INTEGER,remote_config_revision INTEGER,status INTEGER NOT NULL,
 operation_id TEXT NOT NULL,revocation_operation_id TEXT NOT NULL,link_reference TEXT,last_synced_ms INTEGER);
CREATE UNIQUE INDEX IF NOT EXISTS remote_tally_owner_mapping
 ON remote_tally_publications(grant_id,profile_id,tally_id) WHERE status<>5;
CREATE TABLE IF NOT EXISTS remote_tally_outbox(
 publication_id TEXT NOT NULL,operation_id TEXT NOT NULL UNIQUE,kind INTEGER NOT NULL,
 revision INTEGER NOT NULL,config_revision INTEGER NOT NULL,remote_revision INTEGER,
 definition TEXT CHECK(length(definition)<=65536),started INTEGER NOT NULL DEFAULT 0,
 PRIMARY KEY(publication_id,revision));
CREATE TABLE IF NOT EXISTS remote_tally_receipts(
 operation_id TEXT PRIMARY KEY,grant_id TEXT NOT NULL,publication_id TEXT NOT NULL,
 content TEXT NOT NULL CHECK(length(content)<=66560),created_ms INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS remote_tally_receipt_expiry ON remote_tally_receipts(created_ms);
CREATE TRIGGER IF NOT EXISTS remote_tally_forget AFTER DELETE ON devices BEGIN
 DELETE FROM remote_tally_grants WHERE device_id=OLD.id;
END;
CREATE TRIGGER IF NOT EXISTS remote_tally_revoke AFTER UPDATE OF revoked ON devices
 WHEN NEW.revoked<>0 BEGIN DELETE FROM remote_tally_grants WHERE device_id=NEW.id; END;
CREATE TRIGGER IF NOT EXISTS remote_tally_rebind AFTER UPDATE OF token_hash,production_provider ON devices
 WHEN NEW.token_hash<>OLD.token_hash OR NEW.production_provider<>OLD.production_provider
 BEGIN DELETE FROM remote_tally_grants WHERE device_id=NEW.id; END;
CREATE TRIGGER IF NOT EXISTS remote_tally_withdraw AFTER DELETE ON remote_tally_grants BEGIN
 DELETE FROM remote_tally_outbox WHERE started=0 AND publication_id IN
  (SELECT id FROM remote_tally_publications WHERE grant_id=OLD.id AND status NOT IN (4,5));
 UPDATE remote_tally_publications SET status=4,revision=revision+1,
  operation_id=revocation_operation_id,link_reference=NULL WHERE grant_id=OLD.id AND status NOT IN (4,5);
 INSERT OR IGNORE INTO remote_tally_outbox(publication_id,operation_id,kind,revision,config_revision)
  SELECT id,operation_id,2,revision,config_revision FROM remote_tally_publications
  WHERE grant_id=OLD.id AND status=4;
END;
DELETE FROM remote_tally_grants WHERE NOT EXISTS
 (SELECT 1 FROM devices WHERE devices.id=remote_tally_grants.device_id AND revoked=0 AND production_provider='obs');
)sql");
  transaction.commit();
}
} // namespace studio_remote::tally::persistence
