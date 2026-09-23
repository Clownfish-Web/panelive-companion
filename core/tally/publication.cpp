#include <studio_remote/tally/publication.hpp>

namespace studio_remote::tally
{
namespace
{
std::optional<PublicationFailure> editable(const Publication& previous, const Mutation& request)
{
  if (previous.id != request.publication_id || previous.revision != request.expected_revision)
    return PublicationFailure{PublicationError::conflict, "revision-conflict"};
  // Reserve the final revision for mandatory revocation, even after counter exhaustion.
  if (previous.revision >= maximum_revision - 1 || previous.config_revision >= maximum_revision - 1)
    return PublicationFailure{PublicationError::conflict, "revision-exhausted"};
  if (previous.status == PublicationStatus::revoking ||
      previous.status == PublicationStatus::revoked)
    return PublicationFailure{PublicationError::conflict, "publication-retired"};
  return std::nullopt;
}
} // namespace

PublicationResult<Publication> replace_definition(const Publication& previous,
                                                  const Publish& request)
{
  if (const auto failure = editable(previous, request.mutation))
    return *failure;
  if (previous.profile_id != request.profile_id || previous.desired.id != request.definition.id)
    return PublicationFailure{PublicationError::conflict, "identity-conflict"};
  if (const auto failure = validate(request.definition))
    return PublicationFailure{PublicationError::invalid, failure->reason};
  auto result = previous;
  ++result.revision;
  if (previous.desired != request.definition)
    ++result.config_revision;
  result.desired = request.definition;
  result.operation_id = request.mutation.operation_id;
  if (previous.desired != request.definition)
    result.status =
        previous.remote_revision ? PublicationStatus::sync_pending : PublicationStatus::pending;
  return result;
}

PublicationResult<Publication>
retire_or_rotate(const Publication& previous, const Mutation& request, MutationKind kind)
{
  if (kind == MutationKind::publish)
    return PublicationFailure{PublicationError::invalid, "invalid-operation"};
  if (kind == MutationKind::revoke)
  {
    if (previous.id != request.publication_id || previous.revision != request.expected_revision)
      return PublicationFailure{PublicationError::conflict, "revision-conflict"};
    if (previous.status == PublicationStatus::revoked ||
        previous.status == PublicationStatus::revoking)
      return PublicationFailure{PublicationError::conflict, "publication-retired"};
    if (previous.revision >= maximum_revision)
      return PublicationFailure{PublicationError::conflict, "revision-exhausted"};
  }
  else if (const auto failure = editable(previous, request))
    return *failure;
  auto result = previous;
  ++result.revision;
  result.operation_id = request.operation_id;
  result.status =
      kind == MutationKind::revoke ? PublicationStatus::revoking : PublicationStatus::sync_pending;
  if (kind == MutationKind::revoke)
    result.link_secret_reference.reset();
  return result;
}
} // namespace studio_remote::tally
