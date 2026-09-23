#pragma once

#include <studio_remote/ipc/source_thumbnail_mapping.hpp>

#ifdef _WIN32
#include "windows/shared_memory_owner.hpp"
#elif defined(__APPLE__)
#include "macos/shared_memory_owner.hpp"
#endif

#include <cstdint>
#include <optional>
#include <string>

namespace studio_remote::obs_plugin
{

struct ThumbnailRequest
{
  std::uint64_t sequence = 0;
  std::string source_id;
  std::string source_name;
  std::uint32_t maximum_width = 0;
  std::uint32_t maximum_height = 0;
};

class SharedThumbnailMailbox
{
public:
  ~SharedThumbnailMailbox();
  bool open();
  void close();
  std::optional<ThumbnailRequest> request_after(std::uint64_t sequence) const;
  void publish(std::uint64_t sequence,
               ipc::ThumbnailStatus status,
               const std::uint8_t* pixels = nullptr,
               std::uint32_t width = 0,
               std::uint32_t height = 0,
               std::uint32_t stride = 0,
               std::uint64_t captured_ns = 0);

private:
#ifdef _WIN32
  std::unique_ptr<windows::SharedMemoryOwner> mapping_owner_;
#elif defined(__APPLE__)
  std::unique_ptr<macos::SharedMemoryOwner> mapping_owner_;
#else
  int mapping_handle_ = -1;
#endif
  ipc::SourceThumbnailMapping* mapping_ = nullptr;
};

} // namespace studio_remote::obs_plugin
