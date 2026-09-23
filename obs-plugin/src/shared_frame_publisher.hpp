#pragma once

#include <studio_remote/ipc/video_capture_control_mapping.hpp>
#include <studio_remote/ipc/video_frame_mapping.hpp>

#ifdef _WIN32
#include "windows/shared_memory_owner.hpp"
#elif defined(__APPLE__)
#include "macos/shared_memory_owner.hpp"
#endif

#include <cstdint>
#include <string>

namespace studio_remote::obs_plugin
{

enum class VideoTrack
{
  program,
  preview
};

class SharedFramePublisher
{
public:
  SharedFramePublisher();
#ifdef _WIN32
  SharedFramePublisher(std::wstring frame_name, std::wstring control_name);
#elif defined(__APPLE__)
  SharedFramePublisher(std::string frame_name, std::string control_name);
#endif
  ~SharedFramePublisher();

  SharedFramePublisher(const SharedFramePublisher&) = delete;
  SharedFramePublisher& operator=(const SharedFramePublisher&) = delete;

  bool open();
  void close();
  bool publish(VideoTrack track,
               const std::uint8_t* pixels,
               std::uint32_t width,
               std::uint32_t height,
               std::uint32_t source_stride,
               std::uint64_t captured_ns);
  ipc::VideoCaptureDemandSnapshot demand_snapshot(std::uint64_t now_ns) const;
  void reset_capture_generation();

private:
  ipc::VideoTrackMapping& mapping_for(VideoTrack track);

#ifdef _WIN32
  std::wstring frame_mapping_name_;
  std::wstring control_mapping_name_;
  std::unique_ptr<windows::SharedMemoryOwner> mapping_owner_;
#elif defined(__APPLE__)
  std::string frame_mapping_name_;
  std::string control_mapping_name_;
  std::unique_ptr<macos::SharedMemoryOwner> mapping_owner_;
#else
  int mapping_handle_ = -1;
#endif
  ipc::VideoFrameMapping* mapping_ = nullptr;
#ifdef _WIN32
  std::unique_ptr<windows::SharedMemoryOwner> control_owner_;
#elif defined(__APPLE__)
  std::unique_ptr<macos::SharedMemoryOwner> control_owner_;
#else
  int control_handle_ = -1;
#endif
  ipc::VideoCaptureControlMapping* control_ = nullptr;
};

} // namespace studio_remote::obs_plugin
