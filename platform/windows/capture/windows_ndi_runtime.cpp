#include "windows_ndi_runtime.hpp"

#include "platform_services/windows_production_installations.hpp"

#include <Processing.NDI.Lib.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace studio_remote::windows
{
namespace
{

std::string ascii_lower(std::string value)
{
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](unsigned char character)
                 { return static_cast<char>(std::tolower(character)); });
  return value;
}

std::wstring environment_value(const wchar_t* name)
{
  const auto length = GetEnvironmentVariableW(name, nullptr, 0);
  if (length == 0)
    return {};
  std::wstring value(length, L'\0');
  const auto written = GetEnvironmentVariableW(name, value.data(), length);
  if (written == 0 || written >= length)
    return {};
  value.resize(written);
  return value;
}

HMODULE load_ndi_runtime()
{
  constexpr const wchar_t* runtime_names[] = {L"Processing.NDI.Lib.x64.dll",
                                              L"Processing.NDI.Lib.dll"};
  const auto load_from_directory = [&](std::wstring directory) -> HMODULE
  {
    if (directory.empty())
      return nullptr;
    if (directory.back() != L'\\' && directory.back() != L'/')
      directory.push_back(L'\\');
    for (const auto* runtime_name : runtime_names)
      if (auto module = LoadLibraryW((directory + runtime_name).c_str()))
        return module;
    return nullptr;
  };

  for (const auto* runtime_name : runtime_names)
    if (auto module = LoadLibraryW(runtime_name))
      return module;

  for (const auto* variable :
       {L"STUDIO_REMOTE_NDI_RUNTIME_DIR", L"NDI_RUNTIME_DIR_V6", L"NDI_RUNTIME_DIR_V5"})
    if (auto module = load_from_directory(environment_value(variable)))
      return module;

  for (const auto& directory : discover_vmix_ndi_runtime_directories())
    if (auto module = load_from_directory(directory.wstring()))
      return module;
  return nullptr;
}

template <typename Function> Function load_function(HMODULE module, const char* name)
{
  return reinterpret_cast<Function>(GetProcAddress(module, name));
}

} // namespace

VmixNdiSources select_vmix_ndi_sources(const std::vector<NdiSourceDescriptor>& sources,
                                       std::string_view configured_program_source,
                                       std::string_view configured_preview_source)
{
  VmixNdiSources selected;
  for (const auto& source : sources)
  {
    const auto normalized = ascii_lower(source.name);
    const bool is_vmix_source = normalized.find("vmix") != std::string::npos;
    const bool is_program =
        (!configured_program_source.empty() && source.name == configured_program_source) ||
        (configured_program_source.empty() && is_vmix_source &&
         normalized.find("output 1") != std::string::npos);
    const bool is_preview =
        (!configured_preview_source.empty() && source.name == configured_preview_source) ||
        (configured_preview_source.empty() && is_vmix_source &&
         normalized.find("output 2") != std::string::npos);
    if (is_program)
      selected.program = source;
    if (is_preview)
      selected.preview = source;
    if (!is_vmix_source)
      continue;
    if (normalized.find("headphones") != std::string::npos)
      selected.monitor_audio = source;
    if (normalized.find("master") != std::string::npos)
      selected.master_audio = source;
  }

  if (selected.master_audio.name.empty())
    selected.master_audio = selected.program;
  return selected;
}

struct NdiRuntime::Api
{
  using Initialize = decltype(&NDIlib_initialize);
  using Destroy = decltype(&NDIlib_destroy);
  using ReceiverCreate = decltype(&NDIlib_recv_create_v3);
  using ReceiverCapture = decltype(&NDIlib_recv_capture_v3);
  using ReceiverFreeVideo = decltype(&NDIlib_recv_free_video_v2);
  using ReceiverFreeAudio = decltype(&NDIlib_recv_free_audio_v3);
  using ReceiverDestroy = decltype(&NDIlib_recv_destroy);
  using FinderCreate = decltype(&NDIlib_find_create_v2);
  using FinderWait = decltype(&NDIlib_find_wait_for_sources);
  using FinderSources = decltype(&NDIlib_find_get_current_sources);
  using FinderDestroy = decltype(&NDIlib_find_destroy);

  HMODULE module = nullptr;
  Initialize initialize = nullptr;
  Destroy destroy = nullptr;
  ReceiverCreate receiver_create = nullptr;
  ReceiverCapture receiver_capture = nullptr;
  ReceiverFreeVideo receiver_free_video = nullptr;
  ReceiverFreeAudio receiver_free_audio = nullptr;
  ReceiverDestroy receiver_destroy = nullptr;
  FinderCreate finder_create = nullptr;
  FinderWait finder_wait = nullptr;
  FinderSources finder_sources = nullptr;
  FinderDestroy finder_destroy = nullptr;
};

NdiRuntime::NdiRuntime() : api_(std::make_unique<Api>())
{
  api_->module = load_ndi_runtime();
  if (!api_->module)
    throw std::runtime_error("The NDI runtime is not installed");

  api_->initialize = load_function<Api::Initialize>(api_->module, "NDIlib_initialize");
  api_->destroy = load_function<Api::Destroy>(api_->module, "NDIlib_destroy");
  api_->receiver_create = load_function<Api::ReceiverCreate>(api_->module, "NDIlib_recv_create_v3");
  api_->receiver_capture =
      load_function<Api::ReceiverCapture>(api_->module, "NDIlib_recv_capture_v3");
  api_->receiver_free_video =
      load_function<Api::ReceiverFreeVideo>(api_->module, "NDIlib_recv_free_video_v2");
  api_->receiver_free_audio =
      load_function<Api::ReceiverFreeAudio>(api_->module, "NDIlib_recv_free_audio_v3");
  api_->receiver_destroy = load_function<Api::ReceiverDestroy>(api_->module, "NDIlib_recv_destroy");
  api_->finder_create = load_function<Api::FinderCreate>(api_->module, "NDIlib_find_create_v2");
  api_->finder_wait = load_function<Api::FinderWait>(api_->module, "NDIlib_find_wait_for_sources");
  api_->finder_sources =
      load_function<Api::FinderSources>(api_->module, "NDIlib_find_get_current_sources");
  api_->finder_destroy = load_function<Api::FinderDestroy>(api_->module, "NDIlib_find_destroy");

  if (!api_->initialize || !api_->destroy || !api_->receiver_create || !api_->receiver_capture ||
      !api_->receiver_free_video || !api_->receiver_free_audio || !api_->receiver_destroy ||
      !api_->finder_create || !api_->finder_wait || !api_->finder_sources || !api_->finder_destroy)
  {
    FreeLibrary(api_->module);
    api_->module = nullptr;
    throw std::runtime_error("The NDI runtime does not expose the required receiver API");
  }
  if (!api_->initialize())
  {
    FreeLibrary(api_->module);
    api_->module = nullptr;
    throw std::runtime_error("The NDI runtime could not be initialized");
  }
}

NdiRuntime::~NdiRuntime()
{
  if (api_->module)
  {
    api_->destroy();
    FreeLibrary(api_->module);
  }
}

std::vector<NdiSourceDescriptor> NdiRuntime::discover_sources(std::uint32_t wait_ms,
                                                              std::string_view extra_ips) const
{
  NDIlib_find_create_t configuration{};
  configuration.show_local_sources = true;
  const std::string extra_ips_value(extra_ips);
  configuration.p_extra_ips = extra_ips_value.empty() ? nullptr : extra_ips_value.c_str();
  auto finder = api_->finder_create(&configuration);
  if (!finder)
    return {};

  api_->finder_wait(finder, wait_ms);
  std::uint32_t count = 0;
  const auto* sources = api_->finder_sources(finder, &count);
  std::vector<NdiSourceDescriptor> result;
  result.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index)
    if (sources[index].p_ndi_name)
      result.push_back({sources[index].p_ndi_name,
                        sources[index].p_url_address ? sources[index].p_url_address : ""});
  api_->finder_destroy(finder);
  return result;
}

NdiRuntime::Receiver NdiRuntime::create_video_receiver(const NdiSourceDescriptor& source) const
{
  NDIlib_recv_create_v3_t configuration{};
  configuration.source_to_connect_to.p_ndi_name = source.name.c_str();
  configuration.source_to_connect_to.p_url_address =
      source.url_address.empty() ? nullptr : source.url_address.c_str();
  configuration.color_format = NDIlib_recv_color_format_BGRX_BGRA;
  configuration.bandwidth = NDIlib_recv_bandwidth_lowest;
  configuration.allow_video_fields = false;
  configuration.p_ndi_recv_name = "panelive-companion";
  return api_->receiver_create(&configuration);
}

NdiRuntime::Receiver NdiRuntime::create_audio_receiver(const NdiSourceDescriptor& source) const
{
  NDIlib_recv_create_v3_t configuration{};
  configuration.source_to_connect_to.p_ndi_name = source.name.c_str();
  configuration.source_to_connect_to.p_url_address =
      source.url_address.empty() ? nullptr : source.url_address.c_str();
  configuration.bandwidth = NDIlib_recv_bandwidth_audio_only;
  configuration.p_ndi_recv_name = "panelive-companion";
  return api_->receiver_create(&configuration);
}

bool NdiRuntime::capture_video(Receiver receiver, void* video_frame, std::uint32_t timeout_ms) const
{
  auto* video = static_cast<NDIlib_video_frame_v2_t*>(video_frame);
  return api_->receiver_capture(
             static_cast<NDIlib_recv_instance_t>(receiver), video, nullptr, nullptr, timeout_ms) ==
         NDIlib_frame_type_video;
}

bool NdiRuntime::capture_audio(Receiver receiver, void* audio_frame, std::uint32_t timeout_ms) const
{
  auto* audio = static_cast<NDIlib_audio_frame_v3_t*>(audio_frame);
  return api_->receiver_capture(
             static_cast<NDIlib_recv_instance_t>(receiver), nullptr, audio, nullptr, timeout_ms) ==
         NDIlib_frame_type_audio;
}

void NdiRuntime::free_video(Receiver receiver, void* video_frame) const
{
  api_->receiver_free_video(static_cast<NDIlib_recv_instance_t>(receiver),
                            static_cast<NDIlib_video_frame_v2_t*>(video_frame));
}

void NdiRuntime::free_audio(Receiver receiver, void* audio_frame) const
{
  api_->receiver_free_audio(static_cast<NDIlib_recv_instance_t>(receiver),
                            static_cast<NDIlib_audio_frame_v3_t*>(audio_frame));
}

void NdiRuntime::destroy_receiver(Receiver receiver) const
{
  api_->receiver_destroy(static_cast<NDIlib_recv_instance_t>(receiver));
}

std::shared_ptr<NdiRuntime> NdiRuntimeProvider::acquire()
{
  std::lock_guard lock(mutex_);
  auto runtime = runtime_.lock();
  if (!runtime)
  {
    runtime = std::make_shared<NdiRuntime>();
    runtime_ = runtime;
  }
  return runtime;
}

} // namespace studio_remote::windows
