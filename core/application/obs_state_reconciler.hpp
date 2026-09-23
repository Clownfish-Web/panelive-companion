#pragma once

#include "audio_mixer_roster.hpp"
#include "scene_item_properties.hpp"

#include <studio_remote/media/source_media_inspector.hpp>
#include <studio_remote/obs/obs_adapter.hpp>

namespace studio_remote
{

class ObsStateReconciler
{
public:
  ObsStateReconciler(IObsAdapter& obs,
                     AudioMixerRoster& audio_mixer_roster,
                     SceneItemProperties& scene_item_properties,
                     ISourceMediaInspector* source_media_inspector = nullptr);

  json build_baseline();

private:
  json read_scenes(const json& scene_list_response);
  json read_scene_items(const std::string& scene_name, const std::string& scene_uuid);
  json read_group_items(const std::string& group_name,
                        const std::string& group_uuid,
                        std::size_t depth = 0);
  void classify_media_source(json& item);
  json read_audio_inputs();
  json read_audio_master();
  json read_stream_state();
  json read_streaming_destinations(const json& stream_state);
  json read_record_state();
  bool has_configured_stream_destination();

  IObsAdapter& obs_;
  AudioMixerRoster& audio_mixer_roster_;
  SceneItemProperties& scene_item_properties_;
  ISourceMediaInspector* source_media_inspector_ = nullptr;
};

} // namespace studio_remote
