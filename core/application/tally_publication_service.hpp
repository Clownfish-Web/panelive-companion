#pragma once

#include <studio_remote/credentials/credential_store.hpp>
#include <studio_remote/tally/descriptor.hpp>
#include <studio_remote/tally/publication_repository.hpp>
#include <studio_remote/tally/service_health.hpp>

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace studio_remote::tally
{
// Borrowed dependencies outlive every request and the publisher worker. No network I/O or
// asynchronous work is owned here. Only the trusted publisher owner changes enrollment state.
class PublicationService final
{
public:
  PublicationService(IPublicationRepository& repository,
                     ICredentialStore& credentials,
                     std::string viewer_page_base_url);
  void enrollment_changed(PublisherEnrollment enrollment) noexcept;
  void health_changed(ServiceHealth health) noexcept;
  // Refresh only on HTTP/Ready/management workers, outside canonical state locks. Errors become
  // unavailable cached descriptors. Capability publication itself performs no storage I/O.
  void refresh_authorization(std::string_view device);
  PublicationAvailability availability(std::string_view device) const;
  PublicationResult<PublicationDescriptor> get(std::string_view device, std::string_view id);
  PublicationResult<std::vector<PublicationDescriptor>> list(std::string_view device);
  PublicationResult<DescriptorAdmission> publish(std::string_view device, const Publish& request);
  PublicationResult<DescriptorAdmission> rotate(std::string_view device, const Mutation& request);
  PublicationResult<DescriptorAdmission> unpublish(std::string_view device,
                                                   const Mutation& request);

private:
  PublicationResult<PublicationDescriptor> describe(std::string_view device,
                                                    const Publication& publication);
  PublicationResult<DescriptorAdmission> describe_admission(std::string_view device,
                                                            PublicationResult<Admission> result);
  std::optional<PublicationFailure> require_publisher(std::string_view device);

  IPublicationRepository& repository_;
  ICredentialStore& credentials_;
  const std::string viewer_page_base_url_;
  std::atomic<PublisherEnrollment> enrollment_{PublisherEnrollment::required};
  std::atomic<ServiceHealth> health_{ServiceHealth::unchecked};
  struct Authorization
  {
    PublicationAvailability value{false, "permission-required"};
    std::uint64_t refresh_revision = 0;
  };
  mutable std::mutex authorization_mutex_;
  std::uint64_t authorization_revision_ = 0;
  std::unordered_map<std::string, Authorization> authorizations_;
};
} // namespace studio_remote::tally
