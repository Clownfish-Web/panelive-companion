#import "ManagementPairingPreview.h"

#include "../shared/pairing_preview.hpp"

namespace
{
std::span<const std::uint8_t> pixels(PLPairingCode* code)
{
  return {static_cast<const std::uint8_t*>(code.pixels.bytes), code.pixels.length};
}

std::uint64_t unix_seconds()
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
}
} // namespace

@implementation PLPairingPreview
{
  studio_remote::PairingPreview _preview;
}
+ (NSInteger)normalSize
{
  return studio_remote::PairingPreview::normal_size;
}
+ (NSInteger)enlargedSize
{
  return studio_remote::PairingPreview::enlarged_size;
}
- (BOOL)openCode:(PLPairingCode*)code
{
  try
  {
    _preview.open(
        pixels(code), code.expiresUnixSeconds, std::chrono::steady_clock::now(), unix_seconds());
    return YES;
  }
  catch (...)
  {
    _preview.close();
    NSLog(@"Could not display enlarged pairing QR");
    return NO;
  }
}
- (NSInteger)secondsRemainingForCode:(PLPairingCode*)code
{
  return _preview.seconds_remaining(
      pixels(code), code.expiresUnixSeconds, std::chrono::steady_clock::now(), unix_seconds());
}
- (void)close
{
  _preview.close();
}
@end
