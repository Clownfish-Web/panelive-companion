#include "../shared_frame_publisher.hpp"

#include "../../../platform/macos/ipc/macos_ipc_names.hpp"

#include <memory>
#include <unistd.h>
#include <utility>

namespace studio_remote::obs_plugin
{
SharedFramePublisher::SharedFramePublisher()
    : SharedFramePublisher(studio_remote::macos::ipc_names::video_frames,
                           studio_remote::macos::ipc_names::video_capture_control)
{
}
SharedFramePublisher::SharedFramePublisher(std::string frame_name, std::string control_name)
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
  auto frame_owner =
      macos::SharedMemoryOwner::create(frame_mapping_name_, sizeof(ipc::VideoFrameMapping));
  if (!frame_owner)
    return false;
  auto* frames =
      std::construct_at(reinterpret_cast<ipc::VideoFrameMapping*>(frame_owner->bytes().data()));
  frames->structure_size = sizeof(ipc::VideoFrameMapping);
  frames->producer_process_id = static_cast<std::uint32_t>(getpid());
  auto control_owner = macos::SharedMemoryOwner::create(control_mapping_name_,
                                                        sizeof(ipc::VideoCaptureControlMapping));
  if (!control_owner)
    return false;
  auto* control = std::construct_at(
      reinterpret_cast<ipc::VideoCaptureControlMapping*>(control_owner->bytes().data()));
  control->structure_size = sizeof(ipc::VideoCaptureControlMapping);
  control->producer_process_id = static_cast<std::uint32_t>(getpid());
  // Publish member state only after both mappings succeed. Local owners also roll back allocation
  // exceptions, so a later open cannot mistake a partial attempt for a ready publisher.
  mapping_owner_ = std::move(frame_owner);
  control_owner_ = std::move(control_owner);
  mapping_ = frames;
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
