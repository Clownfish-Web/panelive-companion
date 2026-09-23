#include "tally_publication_service.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace studio_remote::tally
{
namespace
{
bool valid_read_key(std::string_view key)
{
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  return key.size() == 43 &&
         std::ranges::all_of(key,
                             [&](char value) { return alphabet.find(value) != alphabet.npos; }) &&
         alphabet.find(key.back()) % 4 == 0;
}

DescriptorStatus descriptor_status(PublicationStatus status)
{
  switch (status)
  {
  case PublicationStatus::pending:
    return DescriptorStatus::pending;
  case PublicationStatus::provisioned:
    return DescriptorStatus::provisioned;
  case PublicationStatus::sync_pending:
    return DescriptorStatus::sync_pending;
  case PublicationStatus::error:
    return DescriptorStatus::error;
  case PublicationStatus::revoking:
    return DescriptorStatus::revoking;
  case PublicationStatus::revoked:
    return DescriptorStatus::revoked;
  }
  throw std::logic_error("Invalid publication status");
}
} // namespace

PublicationService::PublicationService(IPublicationRepository& repository,
                                       ICredentialStore& credentials,
                                       std::string viewer_page_base_url)
    : repository_(repository), credentials_(credentials),
      viewer_page_base_url_(std::move(viewer_page_base_url))
{
  // Production values come from the strict build configuration; injected test values must also
  // preserve fragment-only capabilities and cannot redirect the author to a non-HTTPS scheme.
  if (!viewer_page_base_url_.starts_with("https://") || viewer_page_base_url_.ends_with('/') ||
      viewer_page_base_url_.find_first_of("?#@\r\n") != std::string::npos)
    throw std::invalid_argument("Invalid remote tally viewer base URL");
}

void PublicationService::enrollment_changed(PublisherEnrollment enrollment) noexcept
{
  enrollment_.store(enrollment);
}

void PublicationService::health_changed(ServiceHealth health) noexcept
{
  health_.store(health);
}

void PublicationService::refresh_authorization(std::string_view device)
{
  const std::string id(device);
  std::uint64_t revision = 0;
  {
    std::lock_guard lock(authorization_mutex_);
    // Counter exhaustion fails closed until restart; no refresh identity is reused.
    if (authorization_revision_ == std::numeric_limits<std::uint64_t>::max())
    {
      authorizations_.clear();
      return;
    }
    revision = ++authorization_revision_;
    if (authorizations_.size() >= 1024 && !authorizations_.contains(id))
      authorizations_.erase(authorizations_.begin());
    authorizations_[id].refresh_revision = revision;
  }
  PublicationAvailability value{false, "permission-required"};
  const auto granted = repository_.granted(device);
  if (const auto* failure = std::get_if<PublicationFailure>(&granted))
    value.reason = failure->reason;
  else if (std::get<bool>(granted))
    value = {true, {}};
  std::lock_guard lock(authorization_mutex_);
  // An older read must not restore permission after local withdrawal refreshed this device.
  // Evicted or superseded reads cannot recreate their cache entries. No I/O holds this mutex.
  const auto found = authorizations_.find(id);
  if (found != authorizations_.end() && found->second.refresh_revision == revision)
    found->second.value = std::move(value);
}

PublicationAvailability PublicationService::availability(std::string_view device) const
{
  const auto health = health_.load();
  if (health != ServiceHealth::healthy)
    return {false,
            health == ServiceHealth::unhealthy          ? "remote-service-unhealthy"
            : health == ServiceHealth::invalid_response ? "remote-health-invalid-response"
            : health == ServiceHealth::unchecked        ? "remote-health-unchecked"
                                                        : "remote-service-unavailable"};
  {
    std::lock_guard lock(authorization_mutex_);
    const auto found = authorizations_.find(std::string(device));
    if (found == authorizations_.end())
      return {false, "permission-required"};
    if (!found->second.value.available)
      return found->second.value;
  }
  if (enrollment_.load() != PublisherEnrollment::ready)
    return {false, "publisher-enrollment-required"};
  return {true, {}};
}

std::optional<PublicationFailure> PublicationService::require_publisher(std::string_view device)
{
  const auto granted = repository_.granted(device);
  if (const auto* failure = std::get_if<PublicationFailure>(&granted))
    return *failure;
  if (!std::get<bool>(granted))
    return PublicationFailure{PublicationError::forbidden, "permission-required"};
  if (health_.load() != ServiceHealth::healthy)
    return PublicationFailure{PublicationError::storage_unavailable, "remote-service-unavailable"};
  if (enrollment_.load() != PublisherEnrollment::ready)
    return PublicationFailure{PublicationError::storage_unavailable,
                              "publisher-enrollment-required"};
  return std::nullopt;
}

PublicationResult<PublicationDescriptor>
PublicationService::describe(std::string_view device, const Publication& publication)
try
{
  std::optional<std::string> view_url;
  if (publication.link_secret_reference && publication.status != PublicationStatus::revoking &&
      publication.status != PublicationStatus::revoked)
  {
    const auto key = credentials_.get(*publication.link_secret_reference);
    if (!valid_read_key(key))
      return PublicationFailure{PublicationError::storage_unavailable, "read-link-unavailable"};
    view_url = viewer_page_base_url_ + "/" + publication.id + "#k=" + key;
  }
  // Secret access may take time. Recheck the same grant and reference before disclosure, so a
  // completed rotation, revocation or re-pairing cannot return the previously loaded link.
  const auto current = repository_.get(device, publication.id);
  if (const auto* failure = std::get_if<PublicationFailure>(&current))
    return *failure;
  const auto& verified = std::get<Publication>(current);
  if (verified.grant_id != publication.grant_id || verified.revision != publication.revision ||
      verified.link_secret_reference != publication.link_secret_reference ||
      verified.remote_revision != publication.remote_revision ||
      verified.status != publication.status)
    return PublicationFailure{PublicationError::conflict, "publication-changed"};
  return PublicationDescriptor{publication.id,
                               publication.desired.id,
                               publication.profile_id,
                               publication.revision,
                               publication.config_revision,
                               publication.remote_revision,
                               publication.remote_config_revision,
                               descriptor_status(publication.status),
                               std::nullopt,
                               std::move(view_url),
                               publication.last_synced_unix_ms,
                               publication.operation_id};
}
catch (const std::exception&)
{
  return PublicationFailure{PublicationError::storage_unavailable, "read-link-unavailable"};
}

PublicationResult<PublicationDescriptor> PublicationService::get(std::string_view device,
                                                                 std::string_view id)
{
  const auto value = repository_.get(device, id);
  if (const auto* failure = std::get_if<PublicationFailure>(&value))
    return *failure;
  return describe(device, std::get<Publication>(value));
}

PublicationResult<std::vector<PublicationDescriptor>>
PublicationService::list(std::string_view device)
{
  const auto values = repository_.list(device);
  if (const auto* failure = std::get_if<PublicationFailure>(&values))
    return *failure;
  std::vector<PublicationDescriptor> result;
  for (const auto& value : std::get<std::vector<Publication>>(values))
  {
    auto descriptor = describe(device, value);
    if (const auto* failure = std::get_if<PublicationFailure>(&descriptor))
      return *failure;
    result.push_back(std::get<PublicationDescriptor>(std::move(descriptor)));
  }
  return result;
}

PublicationResult<DescriptorAdmission>
PublicationService::describe_admission(std::string_view device, PublicationResult<Admission> result)
{
  if (const auto* failure = std::get_if<PublicationFailure>(&result))
    return *failure;
  const auto& admitted = std::get<Admission>(result);
  auto descriptor = describe(device, admitted.publication);
  if (const auto* failure = std::get_if<PublicationFailure>(&descriptor))
    return *failure;
  return DescriptorAdmission{std::get<PublicationDescriptor>(std::move(descriptor)),
                             admitted.repeated};
}

PublicationResult<DescriptorAdmission> PublicationService::publish(std::string_view device,
                                                                   const Publish& request)
{
  if (const auto failure = require_publisher(device))
    return *failure;
  return describe_admission(device, repository_.publish(device, request));
}

PublicationResult<DescriptorAdmission> PublicationService::rotate(std::string_view device,
                                                                  const Mutation& request)
{
  if (const auto failure = require_publisher(device))
    return *failure;
  return describe_admission(device, repository_.rotate(device, request));
}

PublicationResult<DescriptorAdmission> PublicationService::unpublish(std::string_view device,
                                                                     const Mutation& request)
{
  // Enrollment loss must not prevent durable local retirement and later remote cleanup.
  return describe_admission(device, repository_.unpublish(device, request));
}
} // namespace studio_remote::tally
