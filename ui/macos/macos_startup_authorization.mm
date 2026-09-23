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
  alert.messageText = @"Companion needs access to its saved Keychain items";
  alert.informativeText =
      @"Companion uses your saved OBS password and Streamlabs API token to connect to those "
       "adapters. It also stores the private key for secure mobile connections in Keychain.\n\n"
       "Choose Continue to let macOS request access to the required items. If a Keychain dialog "
       "asks for a password, use your Mac login/Keychain password.\n\n"
       "An updated development build may need your approval again.";
  [alert addButtonWithTitle:@"Continue"];
  [alert addButtonWithTitle:@"Quit"].keyEquivalent = @"\x1b";
  [NSApp activateIgnoringOtherApps:YES];
  return [alert runModal] == NSAlertFirstButtonReturn;
}
} // namespace studio_remote::macos
