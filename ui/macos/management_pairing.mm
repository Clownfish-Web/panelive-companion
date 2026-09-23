#include "management_pairing.hpp"

#include "../shared/pairing_qr.hpp"

#include <stdexcept>

@interface PLPairingCode ()
- (instancetype)initWithPairing:(const studio_remote::ManagementPairing&)pairing;
@end

@implementation PLPairingCode
- (instancetype)initWithPairing:(const studio_remote::ManagementPairing&)pairing
{
  self = [super init];
  if (self)
  {
    const auto image = studio_remote::make_pairing_qr_image(pairing.payload);
    _pixels = [NSData dataWithBytes:image.pixels.data() length:image.pixels.size()];
    _size = image.size;
    _expiresUnixSeconds = pairing.expires_unix_seconds;
  }
  return self;
}

- (NSData*)pixelsForSize:(NSInteger)size
{
  try
  {
    if (size < 1 || size > 4096)
      throw std::invalid_argument("Pairing QR dimensions are invalid");
    const auto* bytes = static_cast<const std::uint8_t*>(_pixels.bytes);
    const studio_remote::PairingQrImage original{{bytes, bytes + _pixels.length},
                                                 static_cast<int>(_size)};
    const auto image = studio_remote::resample_pairing_qr_image(original, static_cast<int>(size));
    return [NSData dataWithBytes:image.pixels.data() length:image.pixels.size()];
  }
  catch (...)
  {
    NSLog(@"Could not resample pairing QR");
    return nil;
  }
}
@end

namespace studio_remote::macos
{
PLPairingCode* pairing_code(const ManagementPairing& pairing)
{
  return [[PLPairingCode alloc] initWithPairing:pairing];
}
} // namespace studio_remote::macos
