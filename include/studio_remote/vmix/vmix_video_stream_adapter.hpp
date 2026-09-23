#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <studio_remote/common/types.hpp>
#include <studio_remote/media/media.hpp>

namespace studio_remote::vmix
{

struct VideoStreamAvailability
{
  bool program = false;
  bool preview = false;

  bool all_available() const
  {
    return program && preview;
  }
};

struct VideoStreamAdapterCallbacks
{
  using FrameHandler =
      std::function<void(std::string_view track, std::shared_ptr<const RawVideoFrame> frame)>;
  using HealthHandler = std::function<void(std::string_view track, const json& health)>;
  using FrameRateProvider = std::function<std::uint32_t(std::string_view track)>;

  FrameHandler frame;
  HealthHandler health;
  FrameRateProvider target_frame_rate;
};

// Owns the Program and Preview media transport for one vMix adapter runtime. Implementations
// publish frames and detailed per-track health through VideoStreamAdapterCallbacks supplied at
// composition time.
class IVmixVideoStreamAdapter
{
public:
  virtual ~IVmixVideoStreamAdapter() = default;

  virtual void start() = 0;
  virtual void stop() = 0;
  virtual VideoStreamAvailability availability() const = 0;
};

} // namespace studio_remote::vmix
