#include "pairing_preview.hpp"

#include <algorithm>

namespace studio_remote
{
void PairingPreview::open(std::span<const std::uint8_t> pixels,
                          std::uint64_t expires,
                          TimePoint now,
                          std::uint64_t unix_seconds)
{
  close();
  if (!pixels.empty() && unix_seconds < expires)
    active_ = Active{{pixels.begin(), pixels.end()}, expires, now + std::chrono::seconds(30)};
}

int PairingPreview::seconds_remaining(std::span<const std::uint8_t> pixels,
                                      std::uint64_t expires,
                                      TimePoint now,
                                      std::uint64_t unix_seconds)
{
  if (!active_)
    return 0;
  if (expires != active_->expires || unix_seconds >= expires || now >= active_->deadline ||
      !std::ranges::equal(pixels, active_->pixels))
  {
    close();
    return 0;
  }
  return static_cast<int>(std::chrono::ceil<std::chrono::seconds>(active_->deadline - now).count());
}

void PairingPreview::close() noexcept
{
  active_.reset();
}
} // namespace studio_remote
