#include "rtp_ssrc_allocator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace studio_remote
{
namespace
{
std::mt19937_64 seeded_engine()
{
  std::random_device device;
  const auto steady =
      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  const auto system =
      static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
  std::seed_seq seed{static_cast<std::uint32_t>(device()),
                     static_cast<std::uint32_t>(device()),
                     static_cast<std::uint32_t>(device()),
                     static_cast<std::uint32_t>(device()),
                     static_cast<std::uint32_t>(steady),
                     static_cast<std::uint32_t>(steady >> 32U),
                     static_cast<std::uint32_t>(system),
                     static_cast<std::uint32_t>(system >> 32U)};
  return std::mt19937_64(seed);
}
} // namespace

RtpSsrcAllocator::RtpSsrcAllocator() : engine_(seeded_engine())
{
}

RtpSsrcAllocator::RtpSsrcAllocator(std::seed_seq& seed) : engine_(seed)
{
}

std::uint32_t RtpSsrcAllocator::allocate(std::span<const std::uint32_t> taken)
{
  while (true)
  {
    const auto candidate = static_cast<std::uint32_t>(engine_());
    if (candidate != 0 && std::ranges::find(taken, candidate) == taken.end())
      return candidate;
  }
}
} // namespace studio_remote
