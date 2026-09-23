#pragma once

#include <studio_remote/media/video_demand.hpp>

#include <cstdint>

namespace studio_remote
{
/// A capture worker uniquely owns and serializes this paired control resource. Destruction sends
/// final idle demand only to a still-valid adopted producer before releasing native resources.
class IVideoCaptureDemand
{
public:
  virtual ~IVideoCaptureDemand() = default;
  virtual bool publish(VideoDemand demand, std::uint64_t now_ns) = 0;
  virtual std::uint32_t producer_process_id() const noexcept = 0;
  virtual std::uint64_t generation() const noexcept = 0;
};
} // namespace studio_remote
