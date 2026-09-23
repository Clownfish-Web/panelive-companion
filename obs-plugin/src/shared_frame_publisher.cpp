#include "shared_frame_publisher.hpp"

#include <memory>
#include <utility>

namespace studio_remote::obs_plugin
{
SharedFramePublisher::SharedFramePublisher()
    : SharedFramePublisher(ipc::video_frame_mapping_name, ipc::video_capture_control_mapping_name)
{
}

SharedFramePublisher::SharedFramePublisher(std::wstring frame_name, std::wstring control_name)
    : frame_mapping_name_(std::move(frame_name)), control_mapping_name_(std::move(control_name))
{
}

SharedFramePublisher::~SharedFramePublisher()
{
  close();
}

bool SharedFramePublisher::open()
{
  if (mapping_)
    return true;
  auto frames =
      windows::SharedMemoryOwner::create(frame_mapping_name_, sizeof(ipc::VideoFrameMapping));
  if (!frames)
    return false;
  auto commands = windows::SharedMemoryOwner::create(control_mapping_name_,
                                                     sizeof(ipc::VideoCaptureControlMapping));
  if (!commands)
    return false;
  // Acquire both resources before changing retained consumer memory. Publish members only after
  // initialization so a failed second acquisition cannot be mistaken for a ready publisher.
  auto* mapping =
      std::construct_at(reinterpret_cast<ipc::VideoFrameMapping*>(frames->bytes().data()));
  auto* control = std::construct_at(
      reinterpret_cast<ipc::VideoCaptureControlMapping*>(commands->bytes().data()));
  mapping->structure_size = sizeof(ipc::VideoFrameMapping);
  mapping->producer_process_id = GetCurrentProcessId();
  control->structure_size = sizeof(ipc::VideoCaptureControlMapping);
  control->producer_process_id = GetCurrentProcessId();
  mapping_owner_ = std::move(frames);
  control_owner_ = std::move(commands);
  mapping_ = mapping;
  control_ = control;
  return true;
}

void SharedFramePublisher::close()
{
  control_ = nullptr;
  control_owner_.reset();
  mapping_ = nullptr;
  mapping_owner_.reset();
}
} // namespace studio_remote::obs_plugin
