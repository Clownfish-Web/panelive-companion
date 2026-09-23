#include "vmix_audio_stream_supervisor.hpp"

#include <optional>
#include <stdexcept>
#include <utility>

namespace studio_remote::windows
{

VmixAudioStreamSupervisor::VmixAudioStreamSupervisor(
    AdapterFactory adapter_factory,
    std::chrono::milliseconds retry_interval,
    std::chrono::milliseconds unavailable_grace_period)
    : adapter_factory_(std::move(adapter_factory)), retry_interval_(retry_interval),
      unavailable_grace_period_(unavailable_grace_period)
{
  if (!adapter_factory_)
    throw std::invalid_argument("A vMix audio stream adapter factory is required");
  if (retry_interval_ <= std::chrono::milliseconds::zero() ||
      unavailable_grace_period_ < std::chrono::milliseconds::zero())
    throw std::invalid_argument("vMix audio stream retry intervals are invalid");
}

VmixAudioStreamSupervisor::~VmixAudioStreamSupervisor()
{
  stop();
}

void VmixAudioStreamSupervisor::start()
{
  std::lock_guard lock(mutex_);
  if (running_)
    return;
  running_ = true;
  stopping_ = false;
  worker_ = std::thread([this] { run(); });
}

void VmixAudioStreamSupervisor::stop()
{
  std::thread worker;
  {
    std::lock_guard lock(mutex_);
    if (!running_)
      return;
    stopping_ = true;
    worker = std::move(worker_);
  }
  wake_.notify_all();
  worker.join();
  {
    std::lock_guard lock(mutex_);
    running_ = false;
  }
}

void VmixAudioStreamSupervisor::run()
{
  using Clock = std::chrono::steady_clock;
  std::unique_ptr<vmix::IVmixAudioStreamAdapter> adapter;
  std::optional<Clock::time_point> unavailable_since;

  while (true)
  {
    if (!adapter)
    {
      try
      {
        adapter = adapter_factory_();
        if (adapter)
        {
          adapter->start();
          unavailable_since = Clock::now();
        }
      }
      catch (...)
      {
        if (adapter)
          adapter->stop();
        adapter.reset();
        unavailable_since.reset();
      }
    }
    else if (adapter->availability().monitor)
    {
      unavailable_since.reset();
    }
    else if (!unavailable_since)
    {
      unavailable_since = Clock::now();
    }
    else if (Clock::now() - *unavailable_since >= unavailable_grace_period_)
    {
      adapter->stop();
      adapter.reset();
      unavailable_since.reset();
    }

    std::unique_lock lock(mutex_);
    if (wake_.wait_for(lock, retry_interval_, [this] { return stopping_; }))
      break;
  }

  if (adapter)
    adapter->stop();
}

} // namespace studio_remote::windows
