#include "management_teleprompter.hpp"
#include "management_text.hpp"

using studio_remote::macos::native_text;

const NSInteger PLTeleprompterMaximumContentLength =
    studio_remote::teleprompter::maximum_content_bytes;

@interface PLTeleprompterScript ()
- (instancetype)initWithValue:(const studio_remote::teleprompter::Summary&)value;
@end

@implementation PLTeleprompterScript
- (instancetype)initWithValue:(const studio_remote::teleprompter::Summary&)value
{
  self = [super init];
  if (self)
  {
    _identifier = native_text(value.id);
    _name = native_text(value.name);
    _revision = value.revision;
    _createdAtUnixMs = value.created_at_unix_ms;
    _modifiedAtUnixMs = value.modified_at_unix_ms;
    _wordCount = value.word_count;
    _estimatedDurationMs = value.estimated_duration_ms;
  }
  return self;
}
@end

@interface PLTeleprompterSession ()
- (instancetype)initWithValue:(const studio_remote::teleprompter::SessionSnapshot&)value;
@end

@implementation PLTeleprompterSession
- (instancetype)initWithValue:(const studio_remote::teleprompter::SessionSnapshot&)value
{
  self = [super init];
  if (self)
  {
    _identifier = native_text(value.state.id);
    _version = value.state.version;
    _scriptCount = value.scripts.size();
    _memberCount = value.members.size();
  }
  return self;
}
@end

@interface PLTeleprompterState ()
- (instancetype)initWithValue:(const studio_remote::TeleprompterUiState&)value;
@end

@implementation PLTeleprompterState
- (instancetype)initWithValue:(const studio_remote::TeleprompterUiState&)value
{
  self = [super init];
  if (self)
  {
    _libraryView = value.view == studio_remote::TeleprompterView::library;
    NSMutableArray<PLTeleprompterScript*>* library = [NSMutableArray array];
    for (const auto& script : value.library)
      [library addObject:[[PLTeleprompterScript alloc] initWithValue:script]];
    _library = [library copy];
    _deviceIdentifier = native_text(value.device_id);
    _clientName = native_text(value.client_name);
    _error = native_text(value.error);
    _formGeneration = value.form_generation;
    _editing = value.editor.has_value();
    _editingExisting = value.editor && value.editor->script_id.has_value();
    _sharing = value.sharing.has_value();
    _hasUnsavedChanges =
        value.sharing || (value.editor && value.editor->draft != value.editor->original);
    _name = value.editor ? native_text(value.editor->draft.name) : @"";
    _content = value.editor ? native_text(value.editor->draft.content) : @"";
    _readingSpeed = value.editor ? value.editor->draft.words_per_minute : 150;
    NSMutableArray<PLTeleprompterScript*>* scripts = [NSMutableArray array];
    for (const auto& script : value.scripts)
      [scripts addObject:[[PLTeleprompterScript alloc] initWithValue:script]];
    _scripts = [scripts copy];
    NSMutableArray<PLTeleprompterSession*>* sessions = [NSMutableArray array];
    for (const auto& session : value.sessions)
      if (session.state.owner_device_id == value.device_id)
        [sessions addObject:[[PLTeleprompterSession alloc] initWithValue:session]];
    _sessions = [sessions copy];
    NSMutableArray<NSString*>* selectedScripts = [NSMutableArray array];
    NSMutableArray<NSString*>* selectedClients = [NSMutableArray array];
    NSMutableDictionary<NSString*, NSNumber*>* clientRoles = [NSMutableDictionary dictionary];
    _ownerRole = PLTeleprompterRoleMaster;
    if (value.sharing)
    {
      _ownerRole = static_cast<PLTeleprompterRole>(value.sharing->owner_role);
      for (const auto& id : value.sharing->script_ids)
        [selectedScripts addObject:native_text(id)];
      for (const auto& member : value.sharing->members)
      {
        [selectedClients addObject:native_text(member.device_id)];
        clientRoles[native_text(member.device_id)] = @(static_cast<NSInteger>(member.role));
      }
    }
    _selectedScripts = [selectedScripts copy];
    _selectedClients = [selectedClients copy];
    _clientRoles = [clientRoles copy];
    NSMutableDictionary<NSString*, NSArray<NSNumber*>*>* roleChoices =
        [NSMutableDictionary dictionary];
    for (const auto& [id, roles] : value.role_choices)
    {
      NSMutableArray<NSNumber*>* allowed = [NSMutableArray array];
      for (const auto role : roles)
        [allowed addObject:@(static_cast<NSInteger>(role))];
      roleChoices[native_text(id)] = [allowed copy];
    }
    _roleChoices = [roleChoices copy];
  }
  return self;
}
@end

namespace studio_remote::macos
{
PLTeleprompterState* teleprompter_state(const TeleprompterUiState& state)
{
  return [[PLTeleprompterState alloc] initWithValue:state];
}
} // namespace studio_remote::macos
