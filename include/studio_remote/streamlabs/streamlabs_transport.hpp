#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <studio_remote/common/types.hpp>
#include <studio_remote/streamlabs/streamlabs_state.hpp>
#include <utility>

namespace studio_remote::streamlabs
{

class IStreamlabsTransport
{
public:
  using EventCallback = std::function<void(std::string_view, const json&)>;

  virtual ~IStreamlabsTransport() = default;
  virtual void connect() = 0;
  virtual void disconnect() noexcept = 0;
  virtual bool connected() const noexcept = 0;
  virtual std::string desktop_version() const = 0;
  virtual State read_state() = 0;
  virtual bool read_studio_mode_enabled() = 0;
  virtual std::string read_active_scene_id() = 0;
  virtual std::string read_program_scene_id() = 0;
  virtual std::pair<OutputState, OutputState> read_output_states() = 0;
  virtual PerformanceState read_performance() = 0;
  virtual bool read_scene_item_visibility(std::string_view resource_id) = 0;
  virtual AudioSource read_audio_source(std::string_view resource_id) = 0;
  virtual void select_program_scene(std::string_view scene_id) = 0;
  virtual void select_preview_scene(std::string_view scene_id) = 0;
  virtual void set_scene_item_visibility(std::string_view resource_id, bool visible) = 0;
  virtual void set_audio_mute(std::string_view resource_id, bool muted) = 0;
  virtual void set_audio_volume(std::string_view resource_id, double volume_db) = 0;
  virtual void toggle_recording() = 0;
  virtual void toggle_streaming() = 0;
  virtual void execute_studio_transition() = 0;
  virtual void subscribe(EventCallback callback) = 0;
  virtual std::optional<std::vector<ConfiguredAction>> read_configured_actions()
  {
    return std::nullopt;
  }
  virtual void set_studio_mode(bool)
  {
    throw std::runtime_error("Streamlabs Studio Mode control unavailable");
  }
  virtual void select_collection(std::string_view)
  {
    throw std::runtime_error("Streamlabs collection selection unavailable");
  }
};

} // namespace studio_remote::streamlabs
