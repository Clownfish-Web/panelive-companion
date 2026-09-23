#include "macos_source_media_inspector.hpp"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

namespace studio_remote::macos
{
SourceVisualCapability MacosSourceMediaInspector::inspect(const LocalMediaSource& source)
{
  @autoreleasepool
  {
    NSString* path = [[NSString alloc] initWithBytes:source.path.data()
                                              length:source.path.size()
                                            encoding:NSUTF8StringEncoding];
    if (path.length == 0)
      return SourceVisualCapability::unknown;
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path] options:nil];
    // These synchronous accessors remain supported for Objective-C. Loading here mirrors the
    // Windows media-source/presentation-descriptor inspection on the reconciliation thread.
    NSArray<AVAssetTrack*>* tracks = asset.tracks;
    if (!asset || [asset statusOfValueForKey:@"tracks" error:nil] != AVKeyValueStatusLoaded)
      return SourceVisualCapability::unknown;

    bool has_audio = false;
    bool ambiguous = false;
    bool has_video = false;
    for (AVAssetTrack* track in tracks)
    {
      AVMediaType type = track.mediaType;
      if (!type)
      {
        ambiguous = true;
        break;
      }
      if ([type isEqualToString:AVMediaTypeVideo])
        has_video = true;
      else if ([type isEqualToString:AVMediaTypeAudio])
        has_audio = true;
      else
        ambiguous = true;
    }

    if (has_video)
      return SourceVisualCapability::visual;
    if (has_audio && !ambiguous)
      return SourceVisualCapability::audio_only;
    return SourceVisualCapability::unknown;
  }
}
} // namespace studio_remote::macos
