#include "obs_audio_monitor.hpp"

#ifdef __APPLE__
#include "../../platform/macos/ipc/macos_ipc_names.hpp"
#endif
#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <memory>
#include <utility>

namespace studio_remote::obs_plugin
{
namespace
{
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
    ::close(candidate);
    return nullptr;
  }
  auto* address = mmap(nullptr, sizeof(Mapping), PROT_READ | PROT_WRITE, MAP_SHARED, candidate, 0);
  if (address == MAP_FAILED)
  {
    ::close(candidate);
    return nullptr;
  }
  descriptor = candidate;
  return static_cast<Mapping*>(address);
}
#endif

} // namespace

bool ObsAudioMonitor::open_mapping()
{
#ifdef _WIN32
  if (mapping_ && control_)
    return true;
  auto memory = windows::SharedMemoryOwner::create(ipc::audio_monitor_mapping_name,
                                                   sizeof(ipc::AudioMonitorMapping));
  if (!memory)
    return false;
  auto commands = windows::SharedMemoryOwner::create(ipc::audio_monitor_control_mapping_name,
                                                     sizeof(ipc::AudioMonitorControlMapping));
  if (!commands)
    return false;
  auto* audio =
      std::construct_at(reinterpret_cast<ipc::AudioMonitorMapping*>(memory->bytes().data()));
  auto* control = std::construct_at(
      reinterpret_cast<ipc::AudioMonitorControlMapping*>(commands->bytes().data()));
  audio->structure_size = sizeof(ipc::AudioMonitorMapping);
  audio->producer_process_id = GetCurrentProcessId();
  control->structure_size = sizeof(ipc::AudioMonitorControlMapping);
  control->producer_process_id = GetCurrentProcessId();
  mapping_owner_ = std::move(memory);
  control_owner_ = std::move(commands);
  mapping_ = audio;
  control_ = control;
#elif defined(__APPLE__)
  auto memory = macos::SharedMemoryOwner::create(studio_remote::macos::ipc_names::audio_monitor,
                                                 sizeof(ipc::AudioMonitorMapping));
  if (!memory)
    return false;
  auto* audio =
      std::construct_at(reinterpret_cast<ipc::AudioMonitorMapping*>(memory->bytes().data()));
  audio->structure_size = sizeof(ipc::AudioMonitorMapping);
  audio->producer_process_id = static_cast<std::uint32_t>(getpid());
  auto commands =
      macos::SharedMemoryOwner::create(studio_remote::macos::ipc_names::audio_monitor_control,
                                       sizeof(ipc::AudioMonitorControlMapping));
  if (!commands)
    return false;
  auto* control = std::construct_at(
      reinterpret_cast<ipc::AudioMonitorControlMapping*>(commands->bytes().data()));
  control->structure_size = sizeof(ipc::AudioMonitorControlMapping);
  control->producer_process_id = static_cast<std::uint32_t>(getpid());
  mapping_owner_ = std::move(memory);
  control_owner_ = std::move(commands);
  mapping_ = audio;
  control_ = control;
#else
  mapping_ = create_mapping<ipc::AudioMonitorMapping>(ipc::audio_monitor_shared_memory_name,
                                                      mapping_handle_);
  if (!mapping_)
  {
    close_mapping();
    return false;
  }
  std::construct_at(mapping_);
  mapping_->structure_size = sizeof(ipc::AudioMonitorMapping);
  mapping_->producer_process_id = static_cast<std::uint32_t>(getpid());

  control_ = create_mapping<ipc::AudioMonitorControlMapping>(
      ipc::audio_monitor_control_shared_memory_name, control_handle_);
  if (!control_)
  {
    close_mapping();
    return false;
  }
  std::construct_at(control_);
  control_->structure_size = sizeof(ipc::AudioMonitorControlMapping);
  control_->producer_process_id = static_cast<std::uint32_t>(getpid());
#endif
  return true;
}

void ObsAudioMonitor::close_mapping()
{
#ifdef _WIN32
  control_ = nullptr;
  control_owner_.reset();
  mapping_ = nullptr;
  mapping_owner_.reset();
#elif defined(__APPLE__)
  control_ = nullptr;
  control_owner_.reset();
  mapping_ = nullptr;
  mapping_owner_.reset();
#else
  if (control_)
    munmap(control_, sizeof(ipc::AudioMonitorControlMapping));
  if (control_handle_ >= 0)
    ::close(control_handle_);
  if (mapping_)
    munmap(mapping_, sizeof(ipc::AudioMonitorMapping));
  if (mapping_handle_ >= 0)
    ::close(mapping_handle_);
  mapping_ = nullptr;
  mapping_handle_ = -1;
  control_ = nullptr;
  control_handle_ = -1;
#endif
}

} // namespace studio_remote::obs_plugin
