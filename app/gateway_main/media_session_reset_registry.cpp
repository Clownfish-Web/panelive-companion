#include "media_session_reset_registry.hpp"

#include <utility>

namespace studio_remote
{
MediaSessionReset MediaSessionResetRegistry::begin(std::string production_provider,
                                                   std::uint32_t capture_generation,
                                                   const std::vector<std::string>& device_ids)
{
  std::lock_guard lock(mutex_);
  MediaSessionReset reset{production_provider + "-capture-" + std::to_string(capture_generation) +
                              "-reset-" + std::to_string(++sequence_),
                          std::move(production_provider),
                          capture_generation};
  for (const auto& device_id : device_ids)
    pending_[device_id] = reset;
  return reset;
}

std::optional<MediaSessionReset>
MediaSessionResetRegistry::pending(std::string_view device_id) const
{
  std::lock_guard lock(mutex_);
  const auto found = pending_.find(std::string(device_id));
  return found == pending_.end() ? std::nullopt : std::optional(found->second);
}

MediaSessionResetOfferValidation
MediaSessionResetRegistry::validate_offer(std::string_view device_id,
                                          const std::optional<std::string>& media_reset_id) const
{
  const auto expected = pending(device_id);
  if (!expected)
    return {media_reset_id ? MediaSessionResetOfferDisposition::reset_id_unexpected
                           : MediaSessionResetOfferDisposition::not_required,
            std::nullopt};
  if (!media_reset_id)
    return {MediaSessionResetOfferDisposition::reset_id_missing, expected};
  if (*media_reset_id != expected->id)
    return {MediaSessionResetOfferDisposition::reset_id_mismatch, expected};
  return {MediaSessionResetOfferDisposition::accepted, expected};
}

bool MediaSessionResetRegistry::complete_offer(std::string_view device_id,
                                               std::string_view media_reset_id)
{
  std::lock_guard lock(mutex_);
  const auto found = pending_.find(std::string(device_id));
  if (found == pending_.end() || found->second.id != media_reset_id)
    return false;
  pending_.erase(found);
  return true;
}
} // namespace studio_remote
