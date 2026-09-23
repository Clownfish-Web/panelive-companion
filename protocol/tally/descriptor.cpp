#include <studio_remote/tally/protocol.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace studio_remote::remote_tally_protocol
{
namespace
{
using nlohmann::json;
template <class T> json nullable(const std::optional<T>& value)
{
  return value ? json(*value) : json(nullptr);
}

std::string_view status_name(tally::DescriptorStatus status)
{
  using tally::DescriptorStatus;
  switch (status)
  {
  case DescriptorStatus::pending:
    return "pending";
  case DescriptorStatus::provisioned:
    return "provisioned";
  case DescriptorStatus::live:
    return "live";
  case DescriptorStatus::reconnecting:
    return "reconnecting";
  case DescriptorStatus::sync_pending:
    return "sync-pending";
  case DescriptorStatus::error:
    return "error";
  case DescriptorStatus::revoking:
    return "revoking";
  case DescriptorStatus::revoked:
    return "revoked";
  }
  throw std::logic_error("Invalid remote tally descriptor status");
}

json timestamp(const std::optional<std::uint64_t>& unix_ms)
{
  if (!unix_ms)
    return nullptr;
  if (*unix_ms > 253402300799999ULL)
    throw std::runtime_error("Remote tally sync timestamp is outside the RFC3339 range");
  using namespace std::chrono;
  const sys_time<milliseconds> instant{milliseconds(*unix_ms)};
  const auto day = floor<days>(instant);
  const year_month_day date{day};
  const hh_mm_ss time{instant - day};
  std::ostringstream result;
  result.imbue(std::locale::classic());
  result << std::setfill('0') << std::setw(4) << int(date.year()) << '-' << std::setw(2)
         << unsigned(date.month()) << '-' << std::setw(2) << unsigned(date.day()) << 'T'
         << std::setw(2) << time.hours().count() << ':' << std::setw(2) << time.minutes().count()
         << ':' << std::setw(2) << time.seconds().count() << '.' << std::setw(3)
         << time.subseconds().count() << 'Z';
  return result.str();
}
} // namespace

nlohmann::json descriptor_json(const tally::PublicationDescriptor& value)
{
  return {{"version", 1},
          {"publicationId", value.publication_id},
          {"tallyId", value.tally_id},
          {"clientProfileId", value.profile_id},
          {"revision", value.revision},
          {"configRevision", value.config_revision},
          {"remoteRevision", nullable(value.remote_revision)},
          {"remoteConfigRevision", nullable(value.remote_config_revision)},
          {"status", status_name(value.status)},
          {"reason", nullable(value.reason)},
          {"viewUrl", nullable(value.view_url)},
          {"lastSyncedAt", timestamp(value.last_synced_unix_ms)},
          {"operationId", value.operation_id}};
}
} // namespace studio_remote::remote_tally_protocol
