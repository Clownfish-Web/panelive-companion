#pragma once

#include <studio_remote/tally/publication.hpp>

namespace studio_remote::tally
{
// Admission and reads authenticate the caller's paired device identity supplied by the transport.
// Grant methods are trusted local management only. One owned publisher worker claims/acknowledges
// work.
class IPublicationRepository
{
public:
  virtual ~IPublicationRepository() = default;
  virtual PublicationResult<bool> grant(std::string_view device_id) = 0;
  virtual PublicationResult<bool> withdraw(std::string_view device_id) = 0;
  virtual PublicationResult<bool> granted(std::string_view device_id) = 0;
  virtual PublicationResult<Admission> publish(std::string_view device_id,
                                               const Publish& request) = 0;
  virtual PublicationResult<Admission> rotate(std::string_view device_id,
                                              const Mutation& request) = 0;
  virtual PublicationResult<Admission> unpublish(std::string_view device_id,
                                                 const Mutation& request) = 0;
  virtual PublicationResult<Publication> get(std::string_view device_id, std::string_view id) = 0;
  virtual PublicationResult<std::vector<Publication>> list(std::string_view device_id) = 0;
  // An interrupted claim is returned unchanged after restart for idempotent remote recovery.
  virtual PublicationResult<std::optional<PublicationWork>> claim_next() = 0;
  virtual PublicationResult<bool> acknowledge(const PublicationWork& work,
                                              const RemoteCommit& remote) = 0;
};
} // namespace studio_remote::tally
