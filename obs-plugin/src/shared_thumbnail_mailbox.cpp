#include "shared_thumbnail_mailbox.hpp"

#include "shared_thumbnail_publication.hpp"

#ifdef __APPLE__
#include "../../platform/macos/ipc/macos_ipc_names.hpp"
#endif

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstring>
#include <memory>
#include <string>
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

std::string bounded_string(const char* value, std::size_t capacity)
{
  return std::string(value,
#ifdef _WIN32
                     strnlen_s(value, capacity)
#else
                     strnlen(value, capacity)
#endif
  );
}
} // namespace

SharedThumbnailMailbox::~SharedThumbnailMailbox()
{
  close();
}

bool SharedThumbnailMailbox::open()
{
#ifdef _WIN32
  if (mapping_)
    return true;
  auto memory = windows::SharedMemoryOwner::create(ipc::source_thumbnail_mapping_name,
                                                   sizeof(ipc::SourceThumbnailMapping));
  if (!memory)
    return false;
  mapping_ = reinterpret_cast<ipc::SourceThumbnailMapping*>(memory->bytes().data());
  mapping_owner_ = std::move(memory);
#elif defined(__APPLE__)
  mapping_owner_ = macos::SharedMemoryOwner::create(
      studio_remote::macos::ipc_names::source_thumbnails, sizeof(ipc::SourceThumbnailMapping));
  if (!mapping_owner_)
    return false;
  mapping_ = reinterpret_cast<ipc::SourceThumbnailMapping*>(mapping_owner_->bytes().data());
#else
  mapping_ = create_mapping<ipc::SourceThumbnailMapping>(ipc::source_thumbnail_shared_memory_name,
                                                         mapping_handle_);
  if (!mapping_)
    return false;
#endif
  std::construct_at(mapping_);
  mapping_->structure_size = sizeof(ipc::SourceThumbnailMapping);
#ifdef _WIN32
  mapping_->producer_process_id = GetCurrentProcessId();
#else
  mapping_->producer_process_id = static_cast<std::uint32_t>(getpid());
#endif
  return true;
}

void SharedThumbnailMailbox::close()
{
#ifdef _WIN32
  mapping_ = nullptr;
  mapping_owner_.reset();
#elif defined(__APPLE__)
  mapping_ = nullptr;
  mapping_owner_.reset();
#else
  if (mapping_)
    munmap(mapping_, sizeof(ipc::SourceThumbnailMapping));
  if (mapping_handle_ >= 0)
    ::close(mapping_handle_);
  mapping_ = nullptr;
  mapping_handle_ = -1;
#endif
}

std::optional<ThumbnailRequest> SharedThumbnailMailbox::request_after(std::uint64_t sequence) const
{
  if (!mapping_)
    return std::nullopt;
  const auto requested = mapping_->request_sequence.load(std::memory_order_acquire);
  if (requested == 0 || requested == sequence)
    return std::nullopt;
  return ThumbnailRequest{requested,
                          bounded_string(mapping_->source_id, sizeof(mapping_->source_id)),
                          bounded_string(mapping_->source_name, sizeof(mapping_->source_name)),
                          mapping_->maximum_width,
                          mapping_->maximum_height};
}

void SharedThumbnailMailbox::publish(std::uint64_t sequence,
                                     ipc::ThumbnailStatus status,
                                     std::span<const std::uint8_t> pixels,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     std::uint32_t stride,
                                     std::uint64_t captured_ns)
{
  if (!mapping_)
    return;
  publish_thumbnail_response(
      *mapping_, sequence, status, pixels, width, height, stride, captured_ns);
}

} // namespace studio_remote::obs_plugin
