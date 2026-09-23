#include "macos_audio_diagnostic_state.hpp"

#include <studio_remote/common/types.hpp>
#include <studio_remote/media/audio_diagnostic_gain.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <syslog.h>

namespace studio_remote::macos
{
bool MacosAudioDiagnosticPlayer::Impl::expired()
{
  std::lock_guard lock(mutex);
  return expire_locked(monotonic_ns());
}

MacosAudioDiagnosticPlayer::Impl::Block MacosAudioDiagnosticPlayer::Impl::render_block()
{
  Block block;
  std::lock_guard lock(mutex);
  if (status.prebuffering && playout.ready(frames.size()))
    status.prebuffering = false;
  block.playout_active = !status.prebuffering;
  if (!block.playout_active || frames.empty())
    return block;
  const auto frame = std::move(frames.front());
  frames.pop_front();
  const auto gain = audio_diagnostic_gain_multiplier(gain_db);
  for (std::size_t sample = 0; sample < 960; ++sample)
  {
    const auto input = sample * frame->channels;
    const auto left = apply_audio_diagnostic_gain(frame->samples[input], gain);
    const auto right =
        apply_audio_diagnostic_gain(frame->samples[input + (frame->channels == 1 ? 0 : 1)], gain);
    block.samples[sample * 2] = left.value;
    block.samples[sample * 2 + 1] = right.value;
    block.clipped += left.clipped;
    block.clipped += right.clipped;
  }
  block.written = 960;
  return block;
}
void MacosAudioDiagnosticPlayer::Impl::commit_block(const Block& block)
{
  std::lock_guard lock(mutex);
  status.played_frames += block.written;
  status.gain_clipped_samples += block.clipped;
  if (block.playout_active && block.written == 0)
  {
    ++status.output_underruns;
    status.silent_concealment_frames += 960;
    status.prebuffering = true;
    playout.report_underrun();
  }
  else if (block.written == 960)
    playout.report_stable_callback();
}

void MacosAudioDiagnosticPlayer::Impl::observe_clock(MacosAudioOutputEndpoint& endpoint)
{
  const auto clock = endpoint.clock();
  std::optional<double> rate;
  {
    std::lock_guard lock(mutex);
    if (!clock)
      ++status.degraded_clock_reads;
    else
      rate = playout.observe_endpoint(clock->device_position,
                                      clock->correlated_host_100ns,
                                      clock->device_frequency,
                                      frames.size());
    status.endpoint_clock_observations = playout.endpoint_observations();
    status.rate_adjustment_saturations = playout.rate_saturations();
    status.rate_adjustment_ppm = playout.rate_adjustment_ppm();
    status.playout_low_watermark_frames = playout.low_watermark_frames();
    status.playout_target_frames = playout.target_frames();
    status.playout_high_watermark_frames = playout.high_watermark_frames();
  }
  if (!rate)
    return;
  try
  {
    // Conversion at the exact shared limit can round a fraction above +/-250 ppm.
    const auto ppm = std::clamp((*rate / 48'000.0 - 1.0) * 1'000'000.0, -250.0, 250.0);
    endpoint.set_rate_adjustment(ppm);
    std::lock_guard lock(mutex);
    ++status.rate_adjustments;
  }
  catch (const std::exception&)
  {
    std::lock_guard lock(mutex);
    ++status.rate_adjustment_failures;
  }
}

void MacosAudioDiagnosticPlayer::Impl::run_once(std::stop_token stop, const std::string& device)
{
  MacosAudioOutputEndpoint endpoint(device);
  const auto format = endpoint.device_format();
  if (format.channels > std::numeric_limits<std::uint16_t>::max())
    throw std::runtime_error("Unsupported diagnostic output channel count");
  auto registration =
      realtime_audio
          ? realtime_audio->register_current_thread(RealtimeAudioRole::diagnostic_renderer)
          : nullptr;
  std::uint64_t observed_generation;
  {
    std::lock_guard lock(mutex);
    status.endpoint_mix_sample_rate = static_cast<std::uint32_t>(format.sample_rate);
    status.endpoint_mix_channels = static_cast<std::uint16_t>(format.channels);
    if (status.device_recovery_attempts > status.device_recoveries)
      ++status.device_recoveries;
    observed_generation = queue_generation;
  }
  auto last_submission = Clock::now();
  while (!stop.stop_requested() && !expired())
  {
    bool reset;
    {
      std::lock_guard lock(mutex);
      reset = observed_generation != queue_generation;
      observed_generation = queue_generation;
    }
    if (reset)
      endpoint.stop();
    if (endpoint.pending_buffer_frames() < 3840)
    {
      const auto block = render_block();
      if (!endpoint.submit(block.samples))
        throw std::runtime_error("Diagnostic audio buffer ownership changed unexpectedly");
      endpoint.start();
      commit_block(block);
      last_submission = Clock::now();
    }
    else if (Clock::now() - last_submission >= std::chrono::seconds(2))
      throw std::runtime_error("Diagnostic audio output stopped consuming buffers");
    observe_clock(endpoint);
    std::unique_lock wait_lock(wait_mutex);
    wake.wait_for(wait_lock, stop, std::chrono::milliseconds(5), [this] { return expired(); });
  }
  endpoint.stop();
}

void MacosAudioDiagnosticPlayer::Impl::run(std::stop_token stop, const std::string& device)
{
  bool failure_logged = false;
  while (!stop.stop_requested() && !expired())
  {
    {
      std::lock_guard lock(mutex);
      status.error.clear();
      status.playing = true;
      status.prebuffering = true;
      playout.reset();
    }
    try
    {
      run_once(stop, device);
    }
    catch (const std::exception& error)
    {
      std::lock_guard lock(mutex);
      status.error = error.what();
    }
    catch (...)
    {
      std::lock_guard lock(mutex);
      status.error = "Diagnostic audio playback failed";
    }
    {
      std::lock_guard lock(mutex);
      status.playing = false;
      frames.clear();
    }
    if (stop.stop_requested() || expired())
      break;
    if (!failure_logged)
    {
      syslog(LOG_ERR, "Diagnostic audio output failed; retrying selected device");
      failure_logged = true;
    }
    {
      std::lock_guard lock(mutex);
      ++status.device_recovery_attempts;
    }
    std::unique_lock wait_lock(wait_mutex);
    wake.wait_for(wait_lock, stop, std::chrono::milliseconds(500), [this] { return expired(); });
  }
  std::lock_guard lock(mutex);
  status.playing = false;
  frames.clear();
}
} // namespace studio_remote::macos
