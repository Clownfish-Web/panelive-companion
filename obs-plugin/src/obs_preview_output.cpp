#include "obs_preview_output.hpp"

#include <cstdint>
#include <mutex>
#include <utility>

namespace studio_remote::obs_plugin
{
namespace
{

constexpr char output_id[] = "panelive-companion.preview-output";
constexpr char owner_setting[] = "ownerAddress";

} // namespace

struct ObsPreviewOutput::OutputContext
{
  ObsPreviewOutput* owner = nullptr;
  obs_output_t* output = nullptr;
};

ObsPreviewOutput::ObsPreviewOutput(FrameHandler frame_handler)
    : frame_handler_(std::move(frame_handler))
{
  register_output_type();
}

ObsPreviewOutput::~ObsPreviewOutput()
{
  stop();
}

bool ObsPreviewOutput::start(video_t* video, const video_scale_info& conversion)
{
  if (output_)
    return true;
  if (!video)
    return false;

  obs_data_t* settings = obs_data_create();
  obs_data_set_int(settings, owner_setting, reinterpret_cast<std::intptr_t>(this));
  output_ = obs_output_create(output_id, "PaneLive Companion Preview", settings, nullptr);
  obs_data_release(settings);
  if (!output_)
    return false;

  obs_output_set_media(output_, video, nullptr);
  obs_output_set_video_conversion(output_, &conversion);
  if (obs_output_start(output_))
    return true;

  obs_output_release(output_);
  output_ = nullptr;
  return false;
}

void ObsPreviewOutput::stop()
{
  if (!output_)
    return;

  obs_output_stop(output_);
  obs_output_release(output_);
  output_ = nullptr;
}

const char* ObsPreviewOutput::output_name(void*)
{
  return "PaneLive Companion headless Preview output";
}

void* ObsPreviewOutput::create_output(obs_data_t* settings, obs_output_t* output)
{
  const auto address = obs_data_get_int(settings, owner_setting);
  auto* owner = reinterpret_cast<ObsPreviewOutput*>(static_cast<std::intptr_t>(address));
  return owner ? new OutputContext{owner, output} : nullptr;
}

void ObsPreviewOutput::destroy_output(void* data)
{
  delete static_cast<OutputContext*>(data);
}

bool ObsPreviewOutput::start_output(void* data)
{
  auto* context = static_cast<OutputContext*>(data);
  return context && obs_output_begin_data_capture(context->output, 0);
}

void ObsPreviewOutput::stop_output(void* data, std::uint64_t)
{
  auto* context = static_cast<OutputContext*>(data);
  if (context)
    obs_output_end_data_capture(context->output);
}

void ObsPreviewOutput::receive_frame(void* data, video_data* frame)
{
  auto* context = static_cast<OutputContext*>(data);
  if (context && context->owner && frame)
    context->owner->frame_handler_(*frame);
}

void ObsPreviewOutput::register_output_type()
{
  static std::once_flag registration;
  std::call_once(registration,
                 []
                 {
                   obs_output_info information{};
                   information.id = output_id;
                   information.flags = OBS_OUTPUT_VIDEO;
                   information.get_name = output_name;
                   information.create = create_output;
                   information.destroy = destroy_output;
                   information.start = start_output;
                   information.stop = stop_output;
                   information.raw_video = receive_frame;
                   obs_register_output(&information);
                 });
}

} // namespace studio_remote::obs_plugin
