#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace studio_remote
{
struct MediaSessionReset
{
  std::string id;
  std::string production_provider;
  std::uint32_t capture_generation = 0;
};

enum class MediaSessionResetOfferDisposition
{
  not_required,
  accepted,
  reset_id_unexpected,
  reset_id_missing,
  reset_id_mismatch
};

struct MediaSessionResetOfferValidation
{
  MediaSessionResetOfferDisposition disposition = MediaSessionResetOfferDisposition::not_required;
  std::optional<MediaSessionReset> expected;
};

class MediaSessionResetRegistry
{
public:
  MediaSessionReset begin(std::string production_provider,
                          std::uint32_t capture_generation,
                          const std::vector<std::string>& device_ids);
  std::optional<MediaSessionReset> pending(std::string_view device_id) const;
  MediaSessionResetOfferValidation
  validate_offer(std::string_view device_id,
                 const std::optional<std::string>& media_reset_id) const;
  bool complete_offer(std::string_view device_id, std::string_view media_reset_id);

private:
  mutable std::mutex mutex_;
  std::uint64_t sequence_ = 0;
  std::unordered_map<std::string, MediaSessionReset> pending_;
};
} // namespace studio_remote
