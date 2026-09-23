#include "windows_audio_diagnostic_player.hpp"
#include "windows_audio_rate_adjustment.hpp"

#include <studio_remote/common/types.hpp>
#include <studio_remote/ipc/audio_monitor_mapping.hpp>
#include <studio_remote/media/audio_diagnostic_gain.hpp>
#include <studio_remote/media/audio_diagnostic_peak_window.hpp>
#include <studio_remote/media/audio_diagnostic_session_log.hpp>
#include <studio_remote/media/audio_playout_controller.hpp>

#include <Windows.h>
#include <audioclient.h>
#include <propkeydef.h>

// The device property declarations require DEFINE_PROPERTYKEY from propkeydef.h.
// clang-format off
#include <functiondiscoverykeys_devpkey.h>
// clang-format on
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace studio_remote::windows
{
namespace
{
using Microsoft::WRL::ComPtr;
constexpr std::size_t diagnostic_queue_capacity = 16;

class ComScope
{
public:
  ComScope()
  {
    result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(result_) && result_ != RPC_E_CHANGED_MODE)
      throw std::runtime_error("Could not initialize Windows COM for audio diagnostics");
  }

  ~ComScope()
  {
    if (result_ == S_OK || result_ == S_FALSE)
      CoUninitialize();
  }

  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

private:
  HRESULT result_ = E_FAIL;
};

class EventHandle
{
public:
  EventHandle() : value_(CreateEventW(nullptr, FALSE, FALSE, nullptr))
  {
    if (!value_)
      throw std::runtime_error("Could not create the audio render event");
  }

  ~EventHandle()
  {
    if (value_)
      CloseHandle(value_);
  }

  EventHandle(const EventHandle&) = delete;
  EventHandle& operator=(const EventHandle&) = delete;

  HANDLE get() const noexcept
  {
    return value_;
  }

private:
  HANDLE value_ = nullptr;
};

void require_success(HRESULT result, std::string_view operation)
{
  if (SUCCEEDED(result))
    return;
  std::ostringstream message;
  message << operation << " failed with HRESULT 0x" << std::hex << std::uppercase
          << static_cast<std::uint32_t>(result);
  throw std::runtime_error(message.str());
}

std::string utf8(std::wstring_view value)
{
  if (value.empty())
    return {};
  const auto size = WideCharToMultiByte(CP_UTF8,
                                        WC_ERR_INVALID_CHARS,
                                        value.data(),
                                        static_cast<int>(value.size()),
                                        nullptr,
                                        0,
                                        nullptr,
                                        nullptr);
  if (size <= 0)
    return "Unknown audio output";
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8,
                      WC_ERR_INVALID_CHARS,
                      value.data(),
                      static_cast<int>(value.size()),
                      result.data(),
                      size,
                      nullptr,
                      nullptr);
  return result;
}

std::string device_id(IMMDevice& device)
{
  LPWSTR value = nullptr;
  const auto result = device.GetId(&value);
  const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(value, &CoTaskMemFree);
  require_success(result, "Reading the audio device identifier");
  return utf8(value ? std::wstring_view(value) : std::wstring_view{});
}

std::string device_name(IMMDevice& device)
{
  ComPtr<IPropertyStore> properties;
  require_success(device.OpenPropertyStore(STGM_READ, &properties),
                  "Opening audio device properties");
  PROPVARIANT value;
  PropVariantInit(&value);
  const auto clear = [](PROPVARIANT* property) noexcept
  {
    if (FAILED(PropVariantClear(property)))
      OutputDebugStringW(L"Could not release audio device properties.\n");
  };
  const std::unique_ptr<PROPVARIANT, decltype(clear)> cleanup(&value, clear);
  const auto result = properties->GetValue(PKEY_Device_FriendlyName, &value);
  std::string name = "Unknown audio output";
  if (SUCCEEDED(result) && value.vt == VT_LPWSTR && value.pwszVal)
    name = utf8(value.pwszVal);
  return name;
}

WAVEFORMATEXTENSIBLE diagnostic_format()
{
  WAVEFORMATEXTENSIBLE format{};
  format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  format.Format.nChannels = 2;
  format.Format.nSamplesPerSec = 48'000;
  format.Format.wBitsPerSample = 32;
  format.Format.nBlockAlign = 8;
  format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
  format.Format.cbSize = static_cast<WORD>(sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
  format.Samples.wValidBitsPerSample = 32;
  format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
  format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  return format;
}
} // namespace

struct WindowsAudioDiagnosticPlayer::Impl
{
  explicit Impl(RealtimeAudioServices* value) : realtime_audio(value)
  {
  }

  mutable std::mutex mutex;
  std::mutex lifecycle_mutex;
  std::deque<SharedAudioMonitorFrame> frames;
  AudioDiagnosticPlaybackStatus status;
  std::atomic<bool> stopping{false};
  std::atomic<float> gain_linear{1.0F};
  std::thread thread;
  float gain_db = 0.0F;
  std::uint64_t next_sample_index = 0;
  std::uint32_t topology_generation = 0;
  std::uint64_t queue_generation = 0;
  AudioPlayoutController playout;
  AudioDiagnosticPeakWindow peak_window;
  AudioDiagnosticSessionLog diagnostic_log;
  AudioMonitorStatus monitor_status;
  RealtimeAudioServices* realtime_audio = nullptr;

  bool expire_diagnostic_session_locked(std::uint64_t observed_ns) noexcept
  {
    if (!diagnostic_log.expire_if_due(observed_ns))
      return false;
    status.duration_limit_reached = true;
    stopping.store(true, std::memory_order_release);
    return true;
  }

  std::vector<AudioOutputDevice> output_devices() const
  {
    ComScope com;
    ComPtr<IMMDeviceEnumerator> enumerator;
    require_success(
        CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)),
        "Creating the audio device enumerator");
    ComPtr<IMMDeviceCollection> collection;
    require_success(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection),
                    "Enumerating active audio outputs");

    std::string default_id;
    ComPtr<IMMDevice> default_device;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &default_device)))
      default_id = device_id(*default_device.Get());

    UINT count = 0;
    require_success(collection->GetCount(&count), "Counting active audio outputs");
    std::vector<AudioOutputDevice> result;
    result.reserve(count);
    for (UINT index = 0; index < count; ++index)
    {
      ComPtr<IMMDevice> device;
      if (FAILED(collection->Item(index, &device)))
        continue;
      auto id = device_id(*device.Get());
      result.push_back({id, device_name(*device.Get()), id == default_id});
    }
    std::stable_sort(result.begin(),
                     result.end(),
                     [](const auto& left, const auto& right)
                     {
                       if (left.is_default != right.is_default)
                         return left.is_default;
                       return left.name < right.name;
                     });
    return result;
  }

  void run_once(std::string device_identifier)
  {
    try
    {
      ComScope com;
      ComPtr<IMMDeviceEnumerator> enumerator;
      require_success(
          CoCreateInstance(
              __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)),
          "Creating the audio device enumerator");
      const auto wide_size = MultiByteToWideChar(CP_UTF8,
                                                 MB_ERR_INVALID_CHARS,
                                                 device_identifier.data(),
                                                 static_cast<int>(device_identifier.size()),
                                                 nullptr,
                                                 0);
      if (wide_size <= 0)
        throw std::runtime_error("The selected audio device identifier is invalid");
      std::wstring wide_id(static_cast<std::size_t>(wide_size), L'\0');
      MultiByteToWideChar(CP_UTF8,
                          MB_ERR_INVALID_CHARS,
                          device_identifier.data(),
                          static_cast<int>(device_identifier.size()),
                          wide_id.data(),
                          wide_size);
      ComPtr<IMMDevice> device;
      require_success(enumerator->GetDevice(wide_id.c_str(), &device),
                      "Opening the selected audio output");
      ComPtr<IAudioClient> client;
      require_success(device->Activate(__uuidof(IAudioClient),
                                       CLSCTX_ALL,
                                       nullptr,
                                       reinterpret_cast<void**>(client.GetAddressOf())),
                      "Activating the selected audio output");

      WAVEFORMATEX* endpoint_mix_format = nullptr;
      require_success(client->GetMixFormat(&endpoint_mix_format),
                      "Reading the endpoint mix format");
      const std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> endpoint_mix(
          endpoint_mix_format, &CoTaskMemFree);
      {
        std::lock_guard lock(mutex);
        status.endpoint_mix_sample_rate = endpoint_mix->nSamplesPerSec;
        status.endpoint_mix_channels = endpoint_mix->nChannels;
      }

      auto format = diagnostic_format();
      constexpr DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                              AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                              AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
                              AUDCLNT_STREAMFLAGS_RATEADJUST | AUDCLNT_STREAMFLAGS_NOPERSIST;
      require_success(
          client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &format.Format, nullptr),
          "Initializing raw IPC diagnostic playback");
      EventHandle render_event;
      require_success(client->SetEventHandle(render_event.get()), "Setting the audio render event");
      ComPtr<IAudioRenderClient> renderer;
      require_success(client->GetService(IID_PPV_ARGS(&renderer)),
                      "Opening the audio render client");
      ComPtr<IAudioClock> audio_clock;
      require_success(client->GetService(IID_PPV_ARGS(&audio_clock)), "Opening the audio clock");
      ComPtr<IAudioClockAdjustment> clock_adjustment;
      require_success(client->GetService(IID_PPV_ARGS(&clock_adjustment)),
                      "Opening audio clock adjustment");
      WindowsAudioRateAdjustment rate_adjustment(clock_adjustment.Get());
      UINT64 audio_clock_frequency = 0;
      require_success(audio_clock->GetFrequency(&audio_clock_frequency),
                      "Reading the audio clock frequency");
      UINT32 engine_buffer_frames = 0;
      require_success(client->GetBufferSize(&engine_buffer_frames),
                      "Reading the audio engine buffer size");
      const auto realtime_role = engine_buffer_frames < 480
                                     ? RealtimeAudioRole::diagnostic_renderer_low_latency
                                     : RealtimeAudioRole::diagnostic_renderer;
      auto realtime_registration =
          realtime_audio ? realtime_audio->register_current_thread(realtime_role) : nullptr;
      {
        std::lock_guard lock(mutex);
        status.engine_buffer_frames = engine_buffer_frames;
        status.padding_low_watermark = engine_buffer_frames;
      }
      require_success(client->Start(), "Starting raw IPC diagnostic playback");
      {
        std::lock_guard lock(mutex);
        if (status.device_recovery_attempts > status.device_recoveries)
          ++status.device_recoveries;
      }

      SharedAudioMonitorFrame current;
      std::size_t current_offset = 0;
      bool received_audio = false;
      bool prebuffered = false;
      std::uint64_t observed_queue_generation = 0;
      while (!stopping.load(std::memory_order_acquire))
      {
        {
          std::lock_guard lock(mutex);
          expire_diagnostic_session_locked(monotonic_ns());
        }
        if (stopping.load(std::memory_order_acquire))
          break;
        if (WaitForSingleObject(render_event.get(), 100) != WAIT_OBJECT_0)
        {
          UINT32 probe_padding = 0;
          require_success(client->GetCurrentPadding(&probe_padding),
                          "Checking the audio device after an event timeout");
          continue;
        }
        UINT32 padding = 0;
        require_success(client->GetCurrentPadding(&padding), "Reading audio buffer padding");
        const UINT32 available =
            engine_buffer_frames > padding ? engine_buffer_frames - padding : 0;
        if (available == 0)
          continue;
        UINT64 device_position = 0;
        UINT64 correlated_qpc = 0;
        const auto clock_result = audio_clock->GetPosition(&device_position, &correlated_qpc);
        {
          std::lock_guard lock(mutex);
          status.padding_low_watermark = std::min(status.padding_low_watermark, padding);
          status.padding_high_watermark = std::max(status.padding_high_watermark, padding);
          if (FAILED(clock_result))
            ++status.degraded_clock_reads;
          if (observed_queue_generation != queue_generation)
          {
            observed_queue_generation = queue_generation;
            current.reset();
            current_offset = 0;
            prebuffered = false;
          }
          if (!prebuffered && playout.ready(frames.size()))
          {
            prebuffered = true;
            received_audio = true;
          }
          status.prebuffering = !prebuffered;
          if (SUCCEEDED(clock_result))
          {
            const auto queued = frames.size() + (current ? 1U : 0U);
            if (const auto rate = playout.observe_endpoint(
                    device_position, correlated_qpc, audio_clock_frequency, queued))
              rate_adjustment.request(*rate);
            status.endpoint_clock_observations = playout.endpoint_observations();
            status.rate_adjustment_saturations = playout.rate_saturations();
            status.rate_adjustment_ppm = playout.rate_adjustment_ppm();
          }
          status.rate_adjustments = rate_adjustment.successes();
          status.rate_adjustment_failures = rate_adjustment.failures();
          status.playout_low_watermark_frames = playout.low_watermark_frames();
          status.playout_target_frames = playout.target_frames();
          status.playout_high_watermark_frames = playout.high_watermark_frames();
        }
        BYTE* bytes = nullptr;
        require_success(renderer->GetBuffer(available, &bytes),
                        "Acquiring the audio render buffer");
        if (!prebuffered)
        {
          require_success(renderer->ReleaseBuffer(available, AUDCLNT_BUFFERFLAGS_SILENT),
                          "Releasing the prebuffer silence");
          continue;
        }
        auto* output = reinterpret_cast<float*>(bytes);
        std::fill(output, output + static_cast<std::size_t>(available) * 2, 0.0F);
        const auto gain = gain_linear.load(std::memory_order_relaxed);
        std::uint64_t clipped_samples = 0;
        std::size_t written = 0;
        while (written < available)
        {
          if (!current || current_offset >= current->samples.size() / current->channels)
          {
            std::lock_guard lock(mutex);
            current = {};
            current_offset = 0;
            if (!frames.empty())
            {
              current = std::move(frames.front());
              frames.pop_front();
            }
          }
          if (!current)
            break;
          const auto remaining = current->samples.size() / current->channels - current_offset;
          const auto count = std::min<std::size_t>(remaining, available - written);
          for (std::size_t sample = 0; sample < count; ++sample)
          {
            const auto input = (current_offset + sample) * current->channels;
            const auto left = apply_audio_diagnostic_gain(current->samples[input], gain);
            const auto right = apply_audio_diagnostic_gain(
                current->channels == 1 ? current->samples[input] : current->samples[input + 1],
                gain);
            clipped_samples += left.clipped;
            clipped_samples += right.clipped;
            output[(written + sample) * 2] = left.value;
            output[(written + sample) * 2 + 1] = right.value;
          }
          current_offset += count;
          written += count;
        }
        require_success(renderer->ReleaseBuffer(available, 0), "Releasing the audio render buffer");
        std::lock_guard lock(mutex);
        status.played_frames += written;
        status.gain_clipped_samples += clipped_samples;
        if (received_audio && written < available)
        {
          ++status.output_underruns;
          status.silent_concealment_frames += available - written;
          prebuffered = false;
          status.prebuffering = true;
          playout.report_underrun();
        }
        else if (written == available)
          playout.report_stable_callback();
      }
      static_cast<void>(client->Stop());
    }
    catch (const std::exception& error)
    {
      std::lock_guard lock(mutex);
      status.error = error.what();
    }
    std::lock_guard lock(mutex);
    status.playing = false;
    frames.clear();
  }

  void run(std::string device_identifier)
  {
    while (!stopping.load(std::memory_order_acquire))
    {
      {
        std::lock_guard lock(mutex);
        status.error.clear();
        status.playing = true;
        status.prebuffering = true;
        playout.reset();
      }
      run_once(device_identifier);
      if (stopping.load(std::memory_order_acquire))
        break;
      {
        std::lock_guard lock(mutex);
        ++status.device_recovery_attempts;
      }
      for (int tick = 0; tick < 10 && !stopping.load(std::memory_order_acquire); ++tick)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
};

WindowsAudioDiagnosticPlayer::WindowsAudioDiagnosticPlayer(RealtimeAudioServices* realtime_audio)
    : impl_(std::make_unique<Impl>(realtime_audio))
{
}

WindowsAudioDiagnosticPlayer::~WindowsAudioDiagnosticPlayer()
{
  stop();
}

std::vector<AudioOutputDevice> WindowsAudioDiagnosticPlayer::output_devices() const
{
  return impl_->output_devices();
}

void WindowsAudioDiagnosticPlayer::start(std::string_view device_id, AudioDiagnosticTap tap)
{
  if (device_id.empty())
    throw std::invalid_argument("Select an audio output device");
  std::lock_guard lifecycle_lock(impl_->lifecycle_mutex);
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->status.playing)
      impl_->diagnostic_log.playback_stopped(monotonic_ns());
  }
  impl_->stopping.store(true, std::memory_order_release);
  if (impl_->thread.joinable())
    impl_->thread.join();
  impl_->stopping.store(false, std::memory_order_release);
  const auto devices = impl_->output_devices();
  const auto selected =
      std::find_if(devices.begin(),
                   devices.end(),
                   [device_id](const auto& device) { return device.id == device_id; });
  {
    std::lock_guard lock(impl_->mutex);
    impl_->frames.clear();
    impl_->status = {};
    impl_->status.playing = true;
    impl_->status.tap = tap;
    impl_->status.gain_db = impl_->gain_db;
    impl_->status.prebuffering = true;
    impl_->status.device_id = std::string(device_id);
    impl_->next_sample_index = 0;
    impl_->topology_generation = 0;
    ++impl_->queue_generation;
    impl_->playout.reset();
    impl_->peak_window.reset();
    impl_->diagnostic_log.playback_started(tap == AudioDiagnosticTap::pre_opus_ipc
                                               ? AudioDiagnosticLogTap::pre_opus
                                               : AudioDiagnosticLogTap::post_opus,
                                           monotonic_ns());
    if (selected != devices.end())
      impl_->status.device_name = selected->name;
  }
  impl_->thread = std::thread([implementation = impl_.get(), id = std::string(device_id)]
                              { implementation->run(id); });
}

void WindowsAudioDiagnosticPlayer::stop()
{
  std::lock_guard lifecycle_lock(impl_->lifecycle_mutex);
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->status.playing)
      impl_->diagnostic_log.playback_stopped(monotonic_ns());
  }
  impl_->stopping.store(true, std::memory_order_release);
  if (impl_->thread.joinable())
    impl_->thread.join();
  std::lock_guard lock(impl_->mutex);
  impl_->frames.clear();
  impl_->peak_window.reset();
  impl_->status.playing = false;
}

void WindowsAudioDiagnosticPlayer::set_gain_db(float gain_db)
{
  if (!valid_audio_diagnostic_gain(gain_db))
    throw std::invalid_argument("Diagnostic gain must be between -24 dB and +12 dB");
  std::lock_guard lock(impl_->mutex);
  impl_->gain_db = gain_db;
  impl_->gain_linear.store(audio_diagnostic_gain_multiplier(gain_db), std::memory_order_relaxed);
  impl_->status.gain_db = gain_db;
}

void WindowsAudioDiagnosticPlayer::set_monitor_status(const AudioMonitorStatus& status)
{
  std::lock_guard lock(impl_->mutex);
  impl_->monitor_status = status;
}

void WindowsAudioDiagnosticPlayer::push(SharedAudioMonitorFrame frame, AudioDiagnosticTap tap)
{
  if (!frame || frame->sample_rate != 48'000 || (frame->channels != 1 && frame->channels != 2))
    return;
  if (frame->samples.size() != ipc::audio_monitor_frame_samples * frame->channels ||
      !std::all_of(frame->samples.begin(),
                   frame->samples.end(),
                   [](float sample) { return std::isfinite(sample); }))
    return;
  std::lock_guard lock(impl_->mutex);
  if (!impl_->status.playing || impl_->status.tap != tap)
    return;
  const auto observed_ns = monotonic_ns();
  const auto frame_peak = impl_->peak_window.observe(frame->samples, observed_ns);
  ++impl_->status.received_frames;
  const bool discontinuity =
      impl_->next_sample_index != 0 && (frame->topology_generation != impl_->topology_generation ||
                                        frame->first_sample_index != impl_->next_sample_index);
  if (discontinuity)
  {
    impl_->frames.clear();
    ++impl_->queue_generation;
    ++impl_->status.discontinuities;
    impl_->status.prebuffering = true;
    impl_->playout.reset();
  }
  impl_->next_sample_index = frame->first_sample_index + ipc::audio_monitor_frame_samples;
  impl_->topology_generation = frame->topology_generation;
  if (impl_->playout.above_high_watermark(impl_->frames.size() + 1) ||
      impl_->frames.size() == diagnostic_queue_capacity)
  {
    impl_->frames.pop_front();
    ++impl_->queue_generation;
    ++impl_->status.queue_overflows;
    impl_->status.prebuffering = true;
  }
  impl_->frames.push_back(frame);
  const auto peaks = impl_->peak_window.levels(observed_ns);
  AudioDiagnosticLogSnapshot snapshot;
  snapshot.tap = tap == AudioDiagnosticTap::pre_opus_ipc ? AudioDiagnosticLogTap::pre_opus
                                                         : AudioDiagnosticLogTap::post_opus;
  snapshot.observed_ns = observed_ns;
  snapshot.frame_sequence = frame->sequence;
  snapshot.first_sample_index = frame->first_sample_index;
  snapshot.captured_ns = frame->captured_ns;
  snapshot.topology_generation = frame->topology_generation;
  snapshot.frame_flags = frame->flags;
  snapshot.frame_peak = frame_peak;
  snapshot.maximum_peak_last_second = peaks.maximum;
  snapshot.average_peak_last_second = peaks.average;
  snapshot.peak_observations_last_second = peaks.observations;
  snapshot.received_frames = impl_->status.received_frames;
  snapshot.played_frames = impl_->status.played_frames;
  snapshot.queued_frames = impl_->frames.size();
  snapshot.queue_overflows = impl_->status.queue_overflows;
  snapshot.output_underruns = impl_->status.output_underruns;
  snapshot.discontinuities = impl_->status.discontinuities;
  snapshot.silent_concealment_frames = impl_->status.silent_concealment_frames;
  snapshot.prebuffering = impl_->status.prebuffering;
  snapshot.gain_db = impl_->status.gain_db;
  snapshot.gain_clipped_samples = impl_->status.gain_clipped_samples;
  snapshot.monitor_available = impl_->monitor_status.available;
  snapshot.monitor_age_ms = impl_->monitor_status.age_ms;
  snapshot.callback_count = impl_->monitor_status.callback_count;
  snapshot.ingress_overflow_drops = impl_->monitor_status.ingress_overflow_drops;
  snapshot.oversized_callback_drops = impl_->monitor_status.oversized_callback_drops;
  snapshot.ipc_skipped_frames = impl_->monitor_status.ipc_skipped_frames;
  snapshot.timestamp_discontinuities = impl_->monitor_status.timestamp_discontinuities;
  snapshot.timeline_gaps = impl_->monitor_status.timeline_gaps;
  snapshot.timeline_overlaps = impl_->monitor_status.timeline_overlaps;
  snapshot.monitor_latest_peak = impl_->monitor_status.latest_peak;
  snapshot.monitor_latest_rms = impl_->monitor_status.latest_rms;
  impl_->diagnostic_log.record(snapshot);
}

AudioDiagnosticPlaybackStatus WindowsAudioDiagnosticPlayer::status() const
{
  std::lock_guard lock(impl_->mutex);
  auto result = impl_->status;
  result.queued_frames = impl_->frames.size();
  const auto peaks = impl_->peak_window.levels(monotonic_ns());
  result.maximum_peak_last_second = peaks.maximum;
  result.average_peak_last_second = peaks.average;
  result.peak_observations_last_second = peaks.observations;
  return result;
}

void WindowsAudioDiagnosticPlayer::begin_debug_log(std::string_view companion_version,
                                                   std::string_view obs_version)
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto unix_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
  std::lock_guard lock(impl_->mutex);
  impl_->diagnostic_log.begin(companion_version, obs_version, unix_ms, monotonic_ns());
  impl_->status.duration_limit_reached = false;
  if (impl_->status.playing)
  {
    impl_->diagnostic_log.playback_started(impl_->status.tap == AudioDiagnosticTap::pre_opus_ipc
                                               ? AudioDiagnosticLogTap::pre_opus
                                               : AudioDiagnosticLogTap::post_opus,
                                           monotonic_ns());
  }
}

void WindowsAudioDiagnosticPlayer::complete_debug_log()
{
  std::lock_guard lock(impl_->mutex);
  impl_->diagnostic_log.complete(monotonic_ns());
}

bool WindowsAudioDiagnosticPlayer::debug_log_active() const
{
  std::lock_guard lock(impl_->mutex);
  impl_->expire_diagnostic_session_locked(monotonic_ns());
  return impl_->diagnostic_log.active();
}

bool WindowsAudioDiagnosticPlayer::debug_log_available() const
{
  std::lock_guard lock(impl_->mutex);
  return impl_->diagnostic_log.report_available();
}

std::string WindowsAudioDiagnosticPlayer::debug_log_report() const
{
  std::lock_guard lock(impl_->mutex);
  return impl_->diagnostic_log.report();
}

} // namespace studio_remote::windows
