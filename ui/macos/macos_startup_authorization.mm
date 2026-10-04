#include "macos_startup_authorization.hpp"

#import <AppKit/AppKit.h>

namespace studio_remote::macos
{
bool request_keychain_startup_authorization()
{
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  NSAlert* alert = [NSAlert new];
  alert.alertStyle = NSAlertStyleInformational;
  alert.messageText = @"Unlock Companion's credential storage";
  alert.informativeText =
      @"Companion keeps backend passwords and tokens encrypted in your macOS login Keychain. "
       "The Keychain is locked or access is unavailable.\n\n"
       "Choose Continue to let macOS unlock it. If asked, enter your Mac login/Keychain password.";
  [alert addButtonWithTitle:@"Continue"];
  [alert addButtonWithTitle:@"Quit"].keyEquivalent = @"\x1b";
  [NSApp activateIgnoringOtherApps:YES];
  return [alert runModal] == NSAlertFirstButtonReturn;
}
} // namespace studio_remote::macos
