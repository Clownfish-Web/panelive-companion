#pragma once

#include <cstdint>
#include <random>
#include <span>

namespace studio_remote
{
/// Allocates RTP synchronization sources for one peer connection.
///
/// std::random_device is deliberately not used as the sole source: on the Linux development host
/// (AMD Zen 5, GCC 13 libstdc++, RDSEED token) it returned zero for most draws, which produced two
/// audio tracks on SSRC 0 and broke demultiplexing on the receiver. The device is mixed with clock
/// readings into one engine, and an allocation is never zero and never repeats a value already
/// taken by the same peer. Callers serialize access; the class is not thread-safe.
class RtpSsrcAllocator
{
public:
  RtpSsrcAllocator();
  explicit RtpSsrcAllocator(std::seed_seq& seed);

  std::uint32_t allocate(std::span<const std::uint32_t> taken);

private:
  std::mt19937_64 engine_;
};
} // namespace studio_remote
