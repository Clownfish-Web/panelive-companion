#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote::windows
{

struct NdiSourceDescriptor
{
  std::string name;
  std::string url_address;

  bool operator==(const NdiSourceDescriptor&) const = default;
};

struct VmixNdiSources
{
  NdiSourceDescriptor program;
  NdiSourceDescriptor preview;
  NdiSourceDescriptor monitor_audio;
  NdiSourceDescriptor master_audio;
};

VmixNdiSources select_vmix_ndi_sources(const std::vector<NdiSourceDescriptor>& sources,
                                       std::string_view configured_program_source = {},
                                       std::string_view configured_preview_source = {});

// Owns one initialized NDI runtime. Video and audio adapters share this object so process-wide NDI
// initialization outlives every receiver and is destroyed exactly once.
class NdiRuntime
{
public:
  using Receiver = void*;

  NdiRuntime();
  ~NdiRuntime();

  NdiRuntime(const NdiRuntime&) = delete;
  NdiRuntime& operator=(const NdiRuntime&) = delete;

  std::vector<NdiSourceDescriptor> discover_sources(std::uint32_t wait_ms = 2'000,
                                                    std::string_view extra_ips = {}) const;
  Receiver create_video_receiver(const NdiSourceDescriptor& source) const;
  Receiver create_audio_receiver(const NdiSourceDescriptor& source) const;
  bool capture_video(Receiver receiver, void* video_frame, std::uint32_t timeout_ms) const;
  bool capture_audio(Receiver receiver, void* audio_frame, std::uint32_t timeout_ms) const;
  void free_video(Receiver receiver, void* video_frame) const;
  void free_audio(Receiver receiver, void* audio_frame) const;
  void destroy_receiver(Receiver receiver) const;

private:
  struct Api;
  std::unique_ptr<Api> api_;
};

// Lazily shares a runtime between independently supervised NDI video and audio adapters. Failed
// acquisition is retryable and does not make Companion startup fatal.
class NdiRuntimeProvider
{
public:
  std::shared_ptr<NdiRuntime> acquire();

private:
  std::mutex mutex_;
  std::weak_ptr<NdiRuntime> runtime_;
};

} // namespace studio_remote::windows
