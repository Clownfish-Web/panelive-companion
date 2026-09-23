#include "macos_sign_in_startup.hpp"

#include <stdexcept>

namespace studio_remote::macos
{
SignInStartupOperation sign_in_startup_operation(SignInStartupStatus status, bool enabled)
{
  switch (status)
  {
  case SignInStartupStatus::not_registered:
    return enabled ? SignInStartupOperation::register_app : SignInStartupOperation::none;
  case SignInStartupStatus::enabled:
    return enabled ? SignInStartupOperation::none : SignInStartupOperation::unregister_app;
  case SignInStartupStatus::requires_approval:
    if (!enabled)
      return SignInStartupOperation::unregister_app;
    throw std::runtime_error(
        "Allow Companion in System Settings > General > Login Items to enable sign-in startup");
  case SignInStartupStatus::not_found:
    throw std::runtime_error("Could not find the Companion sign-in startup service");
  case SignInStartupStatus::unavailable_bundle:
    throw std::runtime_error("Sign-in startup requires the installed, signed Companion app bundle");
  }
  throw std::invalid_argument("Unknown Companion sign-in startup status");
}
} // namespace studio_remote::macos
