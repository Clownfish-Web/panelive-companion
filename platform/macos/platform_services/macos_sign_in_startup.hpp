#pragma once

namespace studio_remote::macos
{
enum class SignInStartupStatus
{
  not_registered,
  enabled,
  requires_approval,
  not_found,
  unavailable_bundle
};

enum class SignInStartupOperation
{
  none,
  register_app,
  unregister_app
};

/// Determines an idempotent native operation, or reports required system/user action.
SignInStartupOperation sign_in_startup_operation(SignInStartupStatus status, bool enabled);

/// Blocking ServiceManagement adapter, confined to the configuration worker. Only an explicit
/// settings action changes registration. Construction and status reads never register a service.
/// Registration belongs to the canonical signed main application, not a helper or shell command.
class MacosSignInStartup final
{
public:
  SignInStartupStatus status() const;
  bool enabled() const;
  void set_enabled(bool enabled) const;
};
} // namespace studio_remote::macos
