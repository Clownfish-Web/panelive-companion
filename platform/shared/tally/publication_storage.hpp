#pragma once

#include "../storage/sqlite_support.hpp"
#include <studio_remote/tally/publication.hpp>

namespace studio_remote::tally::persistence
{
void initialize(sqlite3* database);
std::optional<std::string> active_grant(sqlite3* database, std::string_view device);
std::optional<Publication> load(sqlite3* database, std::string_view id);
void save(sqlite3* database, const Publication& publication);
std::vector<Publication> owned(sqlite3* database, std::string_view grant);
std::optional<PublicationFailure> capacity(sqlite3* database, std::string_view grant);
std::optional<PublicationFailure>
mutation_budget(sqlite3* database, std::string_view grant, std::uint64_t now);
std::optional<PublicationResult<Admission>> retry(sqlite3* database,
                                                  std::string_view grant,
                                                  const Mutation& mutation,
                                                  std::string_view content);
void receipt(sqlite3* database,
             std::string_view grant,
             const Mutation& mutation,
             std::string_view content,
             std::uint64_t now);
void enqueue(sqlite3* database, const Publication& publication, MutationKind kind);
std::string
request_content(const Mutation& mutation, MutationKind kind, const Publish* publish = nullptr);
std::optional<PublicationWork> next_work(sqlite3* database);
PublicationResult<bool> acknowledge(sqlite3* database,
                                    const PublicationWork& work,
                                    const RemoteCommit& remote,
                                    std::uint64_t now);
inline PublicationFailure unavailable()
{
  return {PublicationError::storage_unavailable, "publication-storage-unavailable"};
}
inline PublicationFailure forbidden()
{
  return {PublicationError::forbidden, "publication-grant-required"};
}
inline PublicationFailure missing()
{
  return {PublicationError::not_found, "publication-not-found"};
}
} // namespace studio_remote::tally::persistence
