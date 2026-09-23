#include <studio_remote/media/media.hpp>
namespace studio_remote
{
// Media policy is deliberately library-independent. Concrete converters and encoders
// are selected by the application composition root.
static_assert(sizeof(FrameId) == sizeof(std::uint64_t));
} // namespace studio_remote
