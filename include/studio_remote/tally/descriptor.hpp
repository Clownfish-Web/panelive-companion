#pragma once

#include <studio_remote/tally/publication.hpp>

namespace studio_remote::tally
{
enum class DescriptorStatus
{
  pending,
  provisioned,
  live,
  reconnecting,
  sync_pending,
  error,
  revoking,
  revoked
};

// Public author data only. No provider endpoint, ownership grant, key reference or definition.
struct PublicationDescriptor
{
  std::string publication_id;
  std::string tally_id;
  std::string profile_id;
  std::uint64_t revision;
  std::uint64_t config_revision;
  std::optional<std::uint64_t> remote_revision;
  std::optional<std::uint64_t> remote_config_revision;
  DescriptorStatus status;
  std::optional<std::string> reason;
  std::optional<std::string> view_url;
  std::optional<std::uint64_t> last_synced_unix_ms;
  std::string operation_id;
};

struct DescriptorAdmission
{
  PublicationDescriptor descriptor;
  bool repeated{false};
};

enum class PublisherEnrollment
{
  required,
  ready
};
struct PublicationAvailability
{
  bool available{false};
  std::string reason;
};
} // namespace studio_remote::tally
