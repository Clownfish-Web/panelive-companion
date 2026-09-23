#include "windows_management_view_adapter.hpp"

#include <utility>

namespace studio_remote::windows
{
ManagementVmixConnection make_management_vmix_connection(const VmixConnectionStatus& status)
{
  auto summary = status.connected && status.synchronized
                     ? std::string("Connected and synchronized with vMix")
                 : status.connected ? std::string("Connected to vMix; synchronizing state")
                                    : std::string("Disconnected from vMix");
  if (!status.detail.empty())
    summary += " - " + status.detail;
  return {status.host,
          status.port,
          status.username,
          std::move(summary),
          status.connected,
          status.srt_program_output,
          status.srt_program_port,
          status.srt_preview_output,
          status.srt_preview_port,
          static_cast<std::uint16_t>(status.srt_latency_ms)};
}

std::vector<ManagementAudioOutput>
make_management_audio_outputs(const std::vector<AudioOutputDevice>& outputs)
{
  std::vector<ManagementAudioOutput> rows;
  rows.reserve(outputs.size());
  for (const auto& output : outputs)
    rows.push_back({output.id, output.name, output.is_default});
  return rows;
}

ManagementAudioDiagnosticStatus make_management_audio_status(
    const AudioDiagnosticPlaybackStatus& status, bool debug_log_active, bool debug_log_available)
{
  return {status.playing,
          status.tap == AudioDiagnosticTap::pre_opus_ipc ? ManagementAudioDiagnosticTap::pre_opus
                                                         : ManagementAudioDiagnosticTap::post_opus,
          status.device_name,
          status.error,
          status.received_frames,
          status.played_frames,
          status.queue_overflows,
          status.output_underruns,
          status.discontinuities,
          status.silent_concealment_frames,
          status.queued_frames,
          status.gain_db,
          status.maximum_peak_last_second,
          status.average_peak_last_second,
          status.duration_limit_reached,
          debug_log_active,
          debug_log_available};
}
} // namespace studio_remote::windows
