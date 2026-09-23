#include "obs_runtime_capabilities_publisher.hpp"

#include "recording_pause_policy.hpp"

#ifdef __APPLE__
#include "../../platform/macos/ipc/macos_ipc_names.hpp"
#endif

#include <obs-frontend-api.h>
#include <obs.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <util/config-file.h>
#include <util/dstr.h>
#include <util/platform.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace studio_remote::obs_plugin
{
namespace
{
constexpr std::uint64_t publication_interval_ns = 250'000'000ULL;

#if !defined(_WIN32) && !defined(__APPLE__)
template <typename Mapping> Mapping* create_mapping(const char* name, int& descriptor)
{
  const auto candidate = shm_open(name, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (candidate < 0)
    return nullptr;
  struct stat status{};
  if (fstat(candidate, &status) != 0 || status.st_uid != geteuid() ||
      fchmod(candidate, 0600) != 0 ||
      ftruncate(candidate, static_cast<off_t>(sizeof(Mapping))) != 0)
  {
    close(candidate);
    return nullptr;
  }
  auto* address = mmap(nullptr, sizeof(Mapping), PROT_READ | PROT_WRITE, MAP_SHARED, candidate, 0);
  if (address == MAP_FAILED)
  {
    close(candidate);
    return nullptr;
  }
  descriptor = candidate;
  return static_cast<Mapping*>(address);
}
#endif

bool equals_ignoring_ascii_case(const char* left, const char* right)
{
  return left && right && astrcmpi(left, right) == 0;
}

RecordingPauseConfiguration current_configuration()
{
  auto* configuration = obs_frontend_get_profile_config();
  const bool advanced =
      configuration &&
      equals_ignoring_ascii_case(config_get_string(configuration, "Output", "Mode"), "Advanced");
  const bool custom_ffmpeg =
      advanced &&
      equals_ignoring_ascii_case(config_get_string(configuration, "AdvOut", "RecType"), "FFmpeg");

  auto* output = obs_frontend_get_recording_output();
  RecordingPauseConfiguration result;
  result.advanced_output_mode = advanced;
  result.custom_ffmpeg_recording = custom_ffmpeg;
  result.ffmpeg_output_to_file =
      configuration && config_get_bool(configuration, "AdvOut", "FFOutputToFile");
  result.recording_encoder =
      configuration ? config_get_string(configuration, "AdvOut", "RecEncoder") : "";
  result.simple_recording_quality =
      configuration ? config_get_string(configuration, "SimpleOutput", "RecQuality") : "";
  result.output_exists = output != nullptr;
  result.output_supports_pause = output && obs_output_can_pause(output);
  if (output)
    obs_output_release(output);
  return result;
}

std::uint64_t monotonic_time_ns()
{
  return os_gettime_ns();
}
} // namespace

ObsRuntimeCapabilitiesPublisher::ObsRuntimeCapabilitiesPublisher() = default;

ObsRuntimeCapabilitiesPublisher::~ObsRuntimeCapabilitiesPublisher()
{
  stop();
}

bool ObsRuntimeCapabilitiesPublisher::start()
{
  if (mapping_)
    return true;

#ifdef _WIN32
  auto memory = windows::SharedMemoryOwner::create(ipc::obs_runtime_capabilities_mapping_name,
                                                   sizeof(ipc::ObsRuntimeCapabilitiesMapping));
  if (!memory)
    return false;
  mapping_ = reinterpret_cast<ipc::ObsRuntimeCapabilitiesMapping*>(memory->bytes().data());
  mapping_owner_ = std::move(memory);
#elif defined(__APPLE__)
  mapping_owner_ =
      macos::SharedMemoryOwner::create(studio_remote::macos::ipc_names::runtime_capabilities,
                                       sizeof(ipc::ObsRuntimeCapabilitiesMapping));
  if (!mapping_owner_)
    return false;
  mapping_ = reinterpret_cast<ipc::ObsRuntimeCapabilitiesMapping*>(mapping_owner_->bytes().data());
#else
  mapping_ = create_mapping<ipc::ObsRuntimeCapabilitiesMapping>(
      ipc::obs_runtime_capabilities_shared_memory_name, mapping_handle_);
  if (!mapping_)
    return false;
#endif

  std::construct_at(mapping_);
#ifdef _WIN32
  mapping_->producer_process_id = GetCurrentProcessId();
#else
  mapping_->producer_process_id = static_cast<std::uint32_t>(getpid());
#endif
  obs_frontend_add_event_callback(handle_frontend_event, this);
  callback_registered_ = true;
  obs_add_tick_callback(on_tick, this);
  tick_registered_ = true;
  publish(false);
  return true;
}

void ObsRuntimeCapabilitiesPublisher::stop()
{
  if (callback_registered_)
    obs_frontend_remove_event_callback(handle_frontend_event, this);
  callback_registered_ = false;
  if (tick_registered_)
    obs_remove_tick_callback(on_tick, this);
  tick_registered_ = false;
  probe_lifecycle_.reset();
  if (mapping_)
  {
    mapping_->recording_pause_available.store(0, std::memory_order_relaxed);
    mapping_->producer_heartbeat_ns.store(0, std::memory_order_release);
#if !defined(_WIN32) && !defined(__APPLE__)
    munmap(mapping_, sizeof(ipc::ObsRuntimeCapabilitiesMapping));
#endif
  }
#ifdef _WIN32
  mapping_ = nullptr;
  mapping_owner_.reset();
#elif defined(__APPLE__)
  mapping_ = nullptr;
  mapping_owner_.reset();
#else
  if (mapping_handle_ >= 0)
    close(mapping_handle_);
  mapping_ = nullptr;
  mapping_handle_ = -1;
#endif
}

void ObsRuntimeCapabilitiesPublisher::on_tick(void* context, float)
{
  if (!context)
    return;
  auto& publisher = *static_cast<ObsRuntimeCapabilitiesPublisher*>(context);
  const auto now = monotonic_time_ns();
  if (now - publisher.last_publication_ns_ < publication_interval_ns)
    return;
  publisher.last_publication_ns_ = now;
  publisher.publish();
}

void ObsRuntimeCapabilitiesPublisher::handle_frontend_event(obs_frontend_event event, void* context)
{
  if (!context)
    return;
  auto& publisher = *static_cast<ObsRuntimeCapabilitiesPublisher*>(context);
  switch (event)
  {
  case OBS_FRONTEND_EVENT_FINISHED_LOADING:
    publisher.frontend_finished_loading();
    break;
  case OBS_FRONTEND_EVENT_EXIT:
    publisher.frontend_exiting();
    break;
  default:
    break;
  }
}

void ObsRuntimeCapabilitiesPublisher::frontend_finished_loading()
{
  probe_lifecycle_.frontend_finished_loading();
  last_publication_ns_ = 0;
}

void ObsRuntimeCapabilitiesPublisher::frontend_exiting()
{
  probe_lifecycle_.reset();
  publish(false);
  // OBS owns and clears the frontend callback list after its EXIT notification.
  callback_registered_ = false;
}

void ObsRuntimeCapabilitiesPublisher::publish()
{
  if (!mapping_)
    return;
  const auto available =
      probe_lifecycle_.ready() && recording_pause_available(current_configuration());
  publish(available);
}

void ObsRuntimeCapabilitiesPublisher::publish(bool recording_pause_available)
{
  if (!mapping_)
    return;
  mapping_->generation.fetch_add(1, std::memory_order_acq_rel);
  mapping_->recording_pause_available.store(recording_pause_available ? 1U : 0U,
                                            std::memory_order_relaxed);
  mapping_->producer_heartbeat_ns.store(monotonic_time_ns(), std::memory_order_relaxed);
  mapping_->generation.fetch_add(1, std::memory_order_release);
}

} // namespace studio_remote::obs_plugin
