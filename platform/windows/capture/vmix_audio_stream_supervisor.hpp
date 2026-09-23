#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <studio_remote/vmix/vmix_audio_stream_adapter.hpp>
#include <thread>

namespace studio_remote::windows
{

class VmixAudioStreamSupervisor
{
public:
  using AdapterFactory = std::function<std::unique_ptr<vmix::IVmixAudioStreamAdapter>()>;

  VmixAudioStreamSupervisor(
      AdapterFactory adapter_factory,
      std::chrono::milliseconds retry_interval = std::chrono::seconds(5),
      std::chrono::milliseconds unavailable_grace_period = std::chrono::seconds(15));
  ~VmixAudioStreamSupervisor();

  VmixAudioStreamSupervisor(const VmixAudioStreamSupervisor&) = delete;
  VmixAudioStreamSupervisor& operator=(const VmixAudioStreamSupervisor&) = delete;

  void start();
  void stop();

private:
  void run();

  AdapterFactory adapter_factory_;
  std::chrono::milliseconds retry_interval_;
  std::chrono::milliseconds unavailable_grace_period_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::thread worker_;
  bool running_ = false;
  bool stopping_ = false;
};

} // namespace studio_remote::windows
