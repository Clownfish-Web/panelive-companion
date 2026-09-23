#include "media_session_reset_protocol.hpp"

namespace studio_remote
{
json media_session_reset_required_message(const MediaSessionReset& reset)
{
  return {{"type", "mediaSessionResetRequired"},
          {"contractRevision", 2},
          {"mediaResetId", reset.id},
          {"productionProvider", reset.production_provider},
          {"captureGeneration", reset.capture_generation},
          {"reason", "obs-video-source-changed"},
          {"tracks", json::array({"program", "preview"})}};
}
} // namespace studio_remote
