#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace studio_remote
{

enum class RealtimeAudioRole
{
  ipc_reader,
  opus_encoder,
  rtp_sender,
  diagnostic_renderer,
  diagnostic_renderer_low_latency
};

class RealtimeThreadRegistration
{
public:
  virtual ~RealtimeThreadRegistration() = default;
  virtual bool active() const noexcept = 0;
};

// High-level audio workers own this interface. Platform adapters provide scheduling and timing
// without leaking operating-system handles into portable code.
class RealtimeAudioServices
{
public:
  virtual ~RealtimeAudioServices() = default;
  virtual std::unique_ptr<RealtimeThreadRegistration>
  register_current_thread(RealtimeAudioRole role) noexcept = 0;
  virtual std::uint64_t performance_counter() const noexcept = 0;
  virtual std::uint64_t performance_frequency() const noexcept = 0;
  virtual std::uint64_t current_thread_cycles() const noexcept = 0;
  virtual bool wait_until(std::uint64_t performance_counter_deadline) noexcept = 0;
  virtual void mark_audio_state(std::string_view state) noexcept = 0;
  virtual bool high_qos_enabled() const noexcept = 0;
};

} // namespace studio_remote
