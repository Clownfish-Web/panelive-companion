#include "linux_audio_output_catalog.hpp"

#include <pulse/error.h>
#include <pulse/pulseaudio.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace studio_remote::linux
{
namespace
{
constexpr auto pulse_operation_timeout = std::chrono::seconds(2);

std::runtime_error pulse_error(std::string_view operation, int error)
{
  return std::runtime_error(std::string(operation) + ": " + pa_strerror(error));
}

class PulseEnumeration
{
public:
  PulseEnumeration()
  {
    mainloop_ = pa_mainloop_new();
    if (!mainloop_)
      throw std::runtime_error("Could not create the PulseAudio event loop");
    context_ = pa_context_new(pa_mainloop_get_api(mainloop_), "PaneLive Companion");
    if (!context_)
    {
      pa_mainloop_free(mainloop_);
      mainloop_ = nullptr;
      throw std::runtime_error("Could not create the PulseAudio client context");
    }
  }

  ~PulseEnumeration()
  {
    if (context_)
    {
      pa_context_disconnect(context_);
      pa_context_unref(context_);
    }
    if (mainloop_)
      pa_mainloop_free(mainloop_);
  }

  PulseEnumeration(const PulseEnumeration&) = delete;
  PulseEnumeration& operator=(const PulseEnumeration&) = delete;

  std::vector<AudioOutputDevice> run()
  {
    if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0)
      throw pulse_error("Connecting to the desktop audio service", pa_context_errno(context_));
    wait_until(
        [this]
        {
          const auto state = pa_context_get_state(context_);
          if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
            throw pulse_error("Connecting to the desktop audio service",
                              pa_context_errno(context_));
          return state == PA_CONTEXT_READY;
        });

    auto* server_operation = pa_context_get_server_info(context_, read_server_info, this);
    if (!server_operation)
      throw pulse_error("Reading the default audio output", pa_context_errno(context_));
    wait_for_operation(server_operation);

    auto* sink_operation = pa_context_get_sink_info_list(context_, read_sink, this);
    if (!sink_operation)
      throw pulse_error("Enumerating audio outputs", pa_context_errno(context_));
    wait_for_operation(sink_operation);
    if (callback_failed_)
      throw std::runtime_error("Could not retain the desktop audio output list");

    for (auto& device : devices_)
      device.is_default = device.id == default_sink_;
    std::stable_sort(devices_.begin(),
                     devices_.end(),
                     [](const auto& left, const auto& right)
                     {
                       if (left.is_default != right.is_default)
                         return left.is_default;
                       return left.name < right.name;
                     });
    return devices_;
  }

private:
  template <typename Predicate> void wait_until(Predicate ready)
  {
    const auto deadline = std::chrono::steady_clock::now() + pulse_operation_timeout;
    while (!ready())
    {
      int result = 0;
      if (pa_mainloop_iterate(mainloop_, 0, &result) < 0)
        throw pulse_error("Running the desktop audio event loop", pa_context_errno(context_));
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error("The desktop audio service did not respond within two seconds");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  void wait_for_operation(pa_operation* operation)
  {
    wait_until([operation] { return pa_operation_get_state(operation) != PA_OPERATION_RUNNING; });
    const auto state = pa_operation_get_state(operation);
    pa_operation_unref(operation);
    if (state != PA_OPERATION_DONE)
      throw pulse_error("Completing the desktop audio request", pa_context_errno(context_));
  }

  static void read_server_info(pa_context*, const pa_server_info* information, void* context)
  {
    auto& self = *static_cast<PulseEnumeration*>(context);
    if (information && information->default_sink_name)
      self.default_sink_ = information->default_sink_name;
  }

  static void read_sink(pa_context*, const pa_sink_info* information, int end, void* context)
  {
    auto& self = *static_cast<PulseEnumeration*>(context);
    if (end != 0 || !information || !information->name)
      return;
    try
    {
      self.devices_.push_back(
          {information->name,
           information->description ? information->description : information->name,
           false});
    }
    catch (...)
    {
      self.callback_failed_ = true;
    }
  }

  pa_mainloop* mainloop_ = nullptr;
  pa_context* context_ = nullptr;
  std::string default_sink_;
  bool callback_failed_ = false;
  std::vector<AudioOutputDevice> devices_;
};
} // namespace

std::vector<AudioOutputDevice> LinuxAudioOutputCatalog::output_devices() const
{
  PulseEnumeration enumeration;
  return enumeration.run();
}

} // namespace studio_remote::linux
