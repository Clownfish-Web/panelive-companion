#include <studio_remote/transport/client_frame_budget.hpp>

#include <algorithm>

namespace studio_remote
{
bool ClientFrameBudget::admit(std::size_t payload_bytes, Clock::time_point now) noexcept
{
  if (!updated_)
    updated_ = now;
  else if (now > *updated_)
  {
    const auto elapsed = std::chrono::duration<double>(now - *updated_).count();
    frames_ = std::min<double>(frame_burst, frames_ + elapsed * frames_per_second);
    bytes_ = std::min<double>(byte_burst, bytes_ + elapsed * bytes_per_second);
    updated_ = now;
  }
  if (payload_bytes > byte_burst || frames_ < 1 || bytes_ < static_cast<double>(payload_bytes))
    return false;
  --frames_;
  bytes_ -= static_cast<double>(payload_bytes);
  return true;
}
} // namespace studio_remote
