#include "management_diagnostics.hpp"

#include "management_text.hpp"

#include <cmath>
#include <sstream>

namespace
{
using studio_remote::macos::native_text;

std::string endurance_summary(const studio_remote::ManagementEnduranceStatus& status)
{
  std::ostringstream output;
  if (!status.file.empty())
    output << "File: " << status.file << "\n";
  output << "Records: " << status.records_written << "  Bytes: " << status.bytes_written
         << "  Retained bytes: " << status.retained_bytes
         << "  Queued bytes: " << status.queued_bytes << "  Queued: " << status.queued_events
         << "  Dropped: " << status.dropped_events;
  return output.str();
}

std::string audio_summary(const studio_remote::ManagementAudioDiagnosticStatus& status)
{
  if (!status.error.empty())
    return status.error;
  if (!status.playing)
    return "Audio diagnostics are idle.";
  return "Playing on " + status.device_name + " - received " +
         std::to_string(status.received_frames) + " frames, played " +
         std::to_string(status.played_frames) + ".";
}
} // namespace

@interface PLAudioOutput ()
- (instancetype)initWithOutput:(const studio_remote::ManagementAudioOutput&)output;
@end

@implementation PLAudioOutput
- (instancetype)initWithOutput:(const studio_remote::ManagementAudioOutput&)output
{
  self = [super init];
  if (self)
  {
    _identifier = native_text(output.id);
    _name = native_text(output.name);
    _isDefault = output.is_default;
  }
  return self;
}
@end

@interface PLDiagnostics ()
- (instancetype)initWithEndurance:(const studio_remote::ManagementEnduranceStatus&)endurance
                          outputs:(const std::vector<studio_remote::ManagementAudioOutput>&)outputs
                            audio:(const studio_remote::ManagementAudioDiagnosticStatus&)audio;
@end

@implementation PLDiagnostics
- (instancetype)initWithEndurance:(const studio_remote::ManagementEnduranceStatus&)endurance
                          outputs:(const std::vector<studio_remote::ManagementAudioOutput>&)outputs
                            audio:(const studio_remote::ManagementAudioDiagnosticStatus&)audio
{
  self = [super init];
  if (self)
  {
    if (!std::isfinite(audio.gain_db) || !std::isfinite(audio.maximum_peak_last_second) ||
        !std::isfinite(audio.average_peak_last_second))
      throw std::invalid_argument("Audio diagnostics contain non-finite values");
    _enduranceEnabled = endurance.enabled;
    _enduranceSummary = native_text(endurance_summary(endurance));
    _enduranceError = native_text(endurance.last_error);
    auto* devices = [NSMutableArray<PLAudioOutput*> arrayWithCapacity:outputs.size()];
    for (const auto& output : outputs)
      [devices addObject:[[PLAudioOutput alloc] initWithOutput:output]];
    _outputs = [devices copy];
    _playing = audio.playing;
    _gain = audio.gain_db;
    _maximumPeak = audio.maximum_peak_last_second;
    _averagePeak = audio.average_peak_last_second;
    _audioSummary = native_text(audio_summary(audio));
    _debugActive = audio.debug_log_active;
    _debugAvailable = audio.debug_log_available;
  }
  return self;
}
@end

namespace studio_remote::macos
{
PLDiagnostics* diagnostics(const ManagementEnduranceStatus& endurance,
                           const std::vector<ManagementAudioOutput>& outputs,
                           const ManagementAudioDiagnosticStatus& audio)
{
  return [[PLDiagnostics alloc] initWithEndurance:endurance outputs:outputs audio:audio];
}

ManagementAudioDiagnosticTap portable_tap(PLDiagnosticTap tap)
{
  switch (tap)
  {
  case PLDiagnosticTapPreOpus:
    return ManagementAudioDiagnosticTap::pre_opus;
  case PLDiagnosticTapPostOpus:
    return ManagementAudioDiagnosticTap::post_opus;
  }
  throw std::invalid_argument("Audio diagnostic tap is invalid");
}
} // namespace studio_remote::macos
