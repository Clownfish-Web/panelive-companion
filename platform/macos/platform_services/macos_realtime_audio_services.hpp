#pragma once

#include <studio_remote/media/realtime_audio_services.hpp>

#include <memory>

namespace studio_remote::macos
{

class MacosRealtimeAudioServices final : public RealtimeAudioServices
{
public:
  MacosRealtimeAudioServices();
  ~MacosRealtimeAudioServices() override;

  MacosRealtimeAudioServices(const MacosRealtimeAudioServices&) = delete;
  MacosRealtimeAudioServices& operator=(const MacosRealtimeAudioServices&) = delete;

  std::unique_ptr<RealtimeThreadRegistration>
  register_current_thread(RealtimeAudioRole role) noexcept override;
  std::uint64_t performance_counter() const noexcept override;
  std::uint64_t performance_frequency() const noexcept override;
  std::uint64_t current_thread_cycles() const noexcept override;
  bool wait_until(std::uint64_t performance_counter_deadline) noexcept override;
  void mark_audio_state(std::string_view state) noexcept override;
  bool high_qos_enabled() const noexcept override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace studio_remote::macos
