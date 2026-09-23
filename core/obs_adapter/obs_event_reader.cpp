#include <studio_remote/obs/obs_event_reader.hpp>

namespace studio_remote
{

std::optional<std::vector<std::string>>
read_audio_meter_input_names(const NormalizedObsEvent& event)
{
  if (event.type != "InputVolumeMeters")
  {
    return std::nullopt;
  }

  const auto inputs = event.data.find("inputs");
  if (inputs == event.data.end() || !inputs->is_array())
  {
    return std::nullopt;
  }

  std::vector<std::string> input_names;
  input_names.reserve(inputs->size());

  for (const auto& input : *inputs)
  {
    const auto input_name = input.find("inputName");
    if (input_name != input.end() && input_name->is_string())
    {
      input_names.push_back(input_name->get<std::string>());
    }
  }

  return input_names;
}

} // namespace studio_remote
