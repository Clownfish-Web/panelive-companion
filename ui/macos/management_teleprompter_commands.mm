#include "management_bridge_commands.hpp"

#include <stdexcept>

namespace
{
std::string text(NSString* value)
{
  NSData* bytes = [value dataUsingEncoding:NSUTF8StringEncoding];
  if (!bytes)
    throw std::invalid_argument("Teleprompter contains invalid text");
  if (!bytes.length)
    return {};
  return {static_cast<const char*>(bytes.bytes), bytes.length};
}
} // namespace

@implementation PLManagementBridge (Teleprompter)
- (void)checkTeleprompterUnsavedChanges:(void (^)(BOOL))completion
{
  __weak PLManagementBridge* target = self;
  [self performCommand:[target, completion](studio_remote::ManagementUiController& controller)
        {
          const bool dirty = controller.teleprompter().has_unsaved_changes();
          auto deliver = ^{
            if (target.valid)
              completion(dirty);
          };
          if ([NSThread isMainThread])
            deliver();
          else
            dispatch_async(dispatch_get_main_queue(), deliver);
        }];
}

- (void)openTeleprompter:(NSString*)deviceIdentifier name:(NSString*)name
{
  [self performCommand:[deviceIdentifier, name](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().open(text(deviceIdentifier), text(name)); }];
}

- (void)closeTeleprompter
{
  [self performCommand:[](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().close(); }];
}

- (void)openTeleprompterLibrary
{
  [self performCommand:[](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().open_library(); }];
}

- (void)attachTeleprompterScript:(NSString*)identifier
{
  [self performCommand:[identifier](studio_remote::ManagementUiController& controller)
        {
          controller.teleprompter().attach_script(text(identifier));
          controller.refresh();
        }];
}

- (void)detachTeleprompterScript:(NSString*)identifier
{
  [self performCommand:[identifier](studio_remote::ManagementUiController& controller)
        {
          controller.teleprompter().detach_script(text(identifier));
          controller.refresh();
        }];
}

- (void)addTeleprompterScript
{
  [self performCommand:[](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().add_script(); }];
}

- (void)editTeleprompterScript:(NSString*)identifier
{
  [self performCommand:[identifier](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().edit_script(text(identifier)); }];
}

- (void)setTeleprompterName:(NSString*)name
{
  [self performCommand:[name](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().set_name(text(name)); }];
}

- (void)setTeleprompterContent:(NSString*)content
{
  [self performCommand:[content](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().set_content(text(content)); }];
}

- (void)setTeleprompterReadingSpeed:(NSInteger)speed
{
  [self performCommand:[speed](studio_remote::ManagementUiController& controller)
        {
          controller.teleprompter().set_reading_speed(
              speed >= 40 && speed <= 400 ? static_cast<std::uint32_t>(speed) : 0);
        }];
}

- (void)saveTeleprompterScript
{
  [self performCommand:[](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().save_script(); }];
}

- (void)cancelTeleprompterEdit
{
  [self performCommand:[](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().cancel_edit(); }];
}

- (void)deleteTeleprompterScript:(NSString*)identifier revision:(uint64_t)revision
{
  [self performCommand:[identifier, revision](studio_remote::ManagementUiController& controller)
        {
          controller.teleprompter().delete_script(text(identifier), revision);
          controller.refresh();
        }];
}

- (void)configureTeleprompterSharing
{
  [self performCommand:[](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().configure_sharing(); }];
}

- (void)selectTeleprompterScript:(NSString*)identifier selected:(BOOL)selected
{
  [self performCommand:[identifier, selected](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().select_shared_script(text(identifier), selected); }];
}

- (void)selectTeleprompterClient:(NSString*)identifier selected:(BOOL)selected
{
  [self performCommand:[identifier, selected](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().select_shared_client(text(identifier), selected); }];
}

- (void)setTeleprompterRole:(NSString*)identifier role:(PLTeleprompterRole)role
{
  [self performCommand:[identifier, role](studio_remote::ManagementUiController& controller)
        {
          if (role < PLTeleprompterRoleMaster || role > PLTeleprompterRoleMonitor)
            throw std::invalid_argument("Invalid teleprompter role");
          controller.teleprompter().set_shared_role(
              text(identifier), static_cast<studio_remote::teleprompter::SessionRole>(role));
        }];
}

- (void)saveTeleprompterSharing
{
  [self performCommand:[](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().save_sharing(); }];
}

- (void)cancelTeleprompterSharing
{
  [self performCommand:[](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().cancel_sharing(); }];
}

- (void)stopTeleprompterSharing:(NSString*)identifier version:(uint64_t)version
{
  [self performCommand:[identifier, version](studio_remote::ManagementUiController& controller)
        { controller.teleprompter().stop_sharing(text(identifier), version); }];
}

@end
