#include "tally_publication_routes.hpp"

#include <studio_remote/tally/protocol.hpp>

#include <algorithm>
#include <limits>

namespace studio_remote
{
namespace
{
namespace wire = remote_tally_protocol;
using namespace tally;
using nlohmann::json;

TallyResponse failure(const PublicationFailure& failure)
{
  int status = 503;
  switch (failure.code)
  {
  case PublicationError::invalid:
    status = 422;
    break;
  case PublicationError::forbidden:
    status = 403;
    break;
  case PublicationError::not_found:
    status = 404;
    break;
  case PublicationError::conflict:
    status = 409;
    break;
  case PublicationError::quota:
    status = 429;
    break;
  case PublicationError::storage_unavailable:
    break;
  }
  return {status,
          {{"error", "remote-tally-rejected"}, {"reason", failure.reason}},
          static_cast<std::uint32_t>(std::min<std::uint64_t>(
              failure.retry_after_ms, std::numeric_limits<std::uint32_t>::max())),
          std::nullopt};
}

TallyResponse malformed(const wire::Error& error)
{
  return {
      error.status,
      {{"error", "invalid-remote-tally-request"}, {"reason", error.reason}, {"field", error.field}},
      0,
      std::nullopt};
}

TallyResponse admitted(PublicationResult<DescriptorAdmission> result)
{
  if (const auto* error = std::get_if<PublicationFailure>(&result))
    return failure(*error);
  const auto& accepted = std::get<DescriptorAdmission>(result);
  const bool complete = accepted.descriptor.status == DescriptorStatus::provisioned ||
                        accepted.descriptor.status == DescriptorStatus::live ||
                        accepted.descriptor.status == DescriptorStatus::revoked;
  return {accepted.repeated && complete ? 200 : 202,
          wire::descriptor_json(accepted.descriptor),
          0,
          accepted.descriptor.publication_id};
}
} // namespace

TallyRoute tally_route(std::string_view path)
{
  constexpr std::string_view collection = "/v1/remote-tallies";
  constexpr std::string_view prefix = "/v1/remote-tallies/";
  constexpr std::string_view rotation = "/rotate-link";
  if (path == collection)
    return {TallyRouteKind::collection, {}};
  if (!path.starts_with(prefix))
    return {};
  auto id = path.substr(prefix.size());
  const bool rotate = id.ends_with(rotation);
  if (rotate)
    id.remove_suffix(rotation.size());
  if (!wire::valid_uuid_v4(id))
    return {};
  return {rotate ? TallyRouteKind::rotate : TallyRouteKind::publication, std::string(id)};
}

TallyResponse read_tally_publications(PublicationService& service,
                                      std::string_view device,
                                      const TallyRoute& route)
{
  if (route.kind == TallyRouteKind::collection)
  {
    const auto result = service.list(device);
    if (const auto* error = std::get_if<PublicationFailure>(&result))
      return failure(*error);
    auto publications = json::array();
    for (const auto& descriptor : std::get<std::vector<PublicationDescriptor>>(result))
      publications.push_back(wire::descriptor_json(descriptor));
    return {200, {{"version", 1}, {"publications", std::move(publications)}}, 0, std::nullopt};
  }
  if (route.kind != TallyRouteKind::publication)
    return {404, {{"error", "not-found"}}, 0, std::nullopt};
  const auto result = service.get(device, route.publication_id);
  if (const auto* error = std::get_if<PublicationFailure>(&result))
    return failure(*error);
  return {200, wire::descriptor_json(std::get<PublicationDescriptor>(result)), 0, std::nullopt};
}

TallyResponse put_tally_publication(PublicationService& service,
                                    std::string_view device,
                                    std::string_view publication_id,
                                    std::string_view body)
{
  const auto result = wire::parse_put(body);
  if (const auto* error = std::get_if<wire::Error>(&result))
    return malformed(*error);
  const auto& request = std::get<wire::PutRequest>(result);
  return admitted(service.publish(
      device,
      {{std::string(publication_id), request.operation_id, request.expected_revision},
       request.client_profile_id,
       request.definition}));
}

TallyResponse delete_tally_publication(PublicationService& service,
                                       std::string_view device,
                                       std::string_view publication_id,
                                       std::string_view body)
{
  const auto result = wire::parse_mutation(body);
  if (const auto* error = std::get_if<wire::Error>(&result))
    return malformed(*error);
  const auto& request = std::get<wire::MutationRequest>(result);
  return admitted(service.unpublish(
      device, {std::string(publication_id), request.operation_id, request.expected_revision}));
}

TallyResponse rotate_tally_link(PublicationService& service,
                                std::string_view device,
                                std::string_view publication_id,
                                std::string_view body)
{
  const auto result = wire::parse_mutation(body);
  if (const auto* error = std::get_if<wire::Error>(&result))
    return malformed(*error);
  const auto& request = std::get<wire::MutationRequest>(result);
  return admitted(service.rotate(
      device, {std::string(publication_id), request.operation_id, request.expected_revision}));
}
} // namespace studio_remote
