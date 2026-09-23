#include "macos_hotkey_executor.hpp"

#include "macos_hotkey_sequence.hpp"

#include <CoreGraphics/CoreGraphics.h>

#include <memory>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace studio_remote::macos
{
namespace
{
struct Release
{
  void operator()(CFTypeRef value) const noexcept
  {
    CFRelease(value);
  }
};
using Source = std::unique_ptr<std::remove_pointer_t<CGEventSourceRef>, Release>;
using Event = std::unique_ptr<std::remove_pointer_t<CGEventRef>, Release>;
} // namespace

bool MacosHotkeyExecutor::available() const noexcept
{
  if (!CGPreflightPostEventAccess())
    return false;
  std::unique_ptr<std::remove_pointer_t<CFDictionaryRef>, Release> session(
      CGSessionCopyCurrentDictionary());
  if (!session || CFDictionaryGetValue(session.get(), kCGSessionOnConsoleKey) != kCFBooleanTrue ||
      CFDictionaryGetValue(session.get(), kCGSessionLoginDoneKey) != kCFBooleanTrue)
    return false;
  const auto user = CFDictionaryGetValue(session.get(), kCGSessionUserIDKey);
  std::int64_t identifier = -1;
  return user && CFGetTypeID(user) == CFNumberGetTypeID() &&
         CFNumberGetValue(static_cast<CFNumberRef>(user), kCFNumberSInt64Type, &identifier) &&
         identifier == static_cast<std::int64_t>(geteuid());
}

void MacosHotkeyExecutor::trigger(std::span<const HotkeyKey> keys)
{
  std::lock_guard lock(posting_mutex_);
  const auto sequence =
      keyboard_sequence(keys, CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState));
  if (!available())
    throw HotkeyExecutionError("input-injection-unavailable",
                               "macOS input injection permission is not available");
  Source source(CGEventSourceCreate(kCGEventSourceStatePrivate));
  if (!source)
    throw std::runtime_error("macOS could not create the keyboard event source");
  std::vector<Event> events;
  events.reserve(sequence.size());
  for (const auto& entry : sequence)
  {
    Event event(CGEventCreateKeyboardEvent(source.get(), entry.key_code, entry.key_down));
    if (!event)
      throw std::runtime_error("macOS could not create the complete hotkey chord");
    CGEventSetFlags(event.get(), entry.flags);
    events.push_back(std::move(event));
  }
  // Allocate and validate the entire chord before emitting anything. CGEventPost has no
  // delivery result; completion means submitted to Quartz, not acknowledged by an application.
  for (const auto& event : events)
    CGEventPost(kCGSessionEventTap, event.get());
}
} // namespace studio_remote::macos
