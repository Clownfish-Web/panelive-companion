#pragma once

#include <memory>
#include <studio_remote/streamlabs/streamlabs_transport.hpp>

namespace studio_remote::streamlabs
{
class StreamlabsRpcTransport;
}

namespace studio_remote::windows
{

class WindowsStreamlabsNamedPipeTransport final
    : public studio_remote::streamlabs::IStreamlabsTransport
{
public:
  WindowsStreamlabsNamedPipeTransport();
  ~WindowsStreamlabsNamedPipeTransport() override;

  void connect() override;
  void disconnect() noexcept override;
  bool connected() const noexcept override;
  std::string desktop_version() const override;
  studio_remote::streamlabs::State read_state() override;
  bool read_studio_mode_enabled() override;
  std::string read_active_scene_id() override;
  std::string read_program_scene_id() override;
  std::pair<studio_remote::streamlabs::OutputState, studio_remote::streamlabs::OutputState>
  read_output_states() override;
  studio_remote::streamlabs::PerformanceState read_performance() override;
  bool read_scene_item_visibility(std::string_view resource_id) override;
  studio_remote::streamlabs::AudioSource read_audio_source(std::string_view resource_id) override;
  void select_program_scene(std::string_view scene_id) override;
  void select_preview_scene(std::string_view scene_id) override;
  void set_scene_item_visibility(std::string_view resource_id, bool visible) override;
  void set_audio_mute(std::string_view resource_id, bool muted) override;
  void set_audio_volume(std::string_view resource_id, double volume_db) override;
  void toggle_recording() override;
  void toggle_streaming() override;
  void execute_studio_transition() override;
  void subscribe(EventCallback callback) override;
  std::optional<std::vector<studio_remote::streamlabs::ConfiguredAction>>
  read_configured_actions() override;
  void set_studio_mode(bool enabled) override;
  void select_collection(std::string_view id) override;

private:
  std::unique_ptr<studio_remote::streamlabs::StreamlabsRpcTransport> impl_;
};

} // namespace studio_remote::windows
