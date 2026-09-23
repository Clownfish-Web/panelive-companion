#pragma once

#include <studio_remote/tally/publication_repository.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>

struct sqlite3;

namespace studio_remote::tally
{
// Owns its SQL connection, sharing the existing AuditStore database. storage_mutex_ serializes
// short durable transactions only. Clock and randomness callbacks always run outside this mutex.
class SqlitePublicationRepository final : public IPublicationRepository
{
public:
  SqlitePublicationRepository(const std::filesystem::path& database_path,
                              std::function<std::string()> uuid,
                              std::function<std::uint64_t()> unix_ms);
  ~SqlitePublicationRepository() override;
  PublicationResult<bool> grant(std::string_view device_id) override;
  PublicationResult<bool> withdraw(std::string_view device_id) override;
  PublicationResult<bool> granted(std::string_view device_id) override;
  PublicationResult<Admission> publish(std::string_view device_id, const Publish& request) override;
  PublicationResult<Admission> rotate(std::string_view device_id, const Mutation& request) override;
  PublicationResult<Admission> unpublish(std::string_view device_id,
                                         const Mutation& request) override;
  PublicationResult<Publication> get(std::string_view device_id, std::string_view id) override;
  PublicationResult<std::vector<Publication>> list(std::string_view device_id) override;
  PublicationResult<std::optional<PublicationWork>> claim_next() override;
  PublicationResult<bool> acknowledge(const PublicationWork& work,
                                      const RemoteCommit& remote) override;

private:
  PublicationResult<Admission>
  mutate(std::string_view device_id, const Mutation& request, MutationKind kind);
  struct Closer
  {
    void operator()(sqlite3* database) const noexcept;
  };
  std::unique_ptr<sqlite3, Closer> database_;
  std::function<std::string()> uuid_;
  std::function<std::uint64_t()> unix_ms_;
  std::mutex storage_mutex_;
};
} // namespace studio_remote::tally
