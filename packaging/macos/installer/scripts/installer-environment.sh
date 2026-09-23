#!/bin/bash
# Original-user actions correspond to Inno Setup's runasoriginaluser operations.

installer_fail() {
  printf '%s\n' "$1" >&2
  exit 1
}

installer_context() {
  [[ "$1" == / ]] || installer_fail 'Install Companion on the currently running macOS volume.'
  [[ "$EUID" -eq 0 ]] || installer_fail 'Open this package with the macOS Installer application.'
  installer_user=$(/usr/bin/stat -f '%Su' /dev/console) ||
    installer_fail 'Could not identify the logged-in user.'
  case "$installer_user" in
    '' | root | loginwindow | _*) installer_fail 'Log in to macOS before installing Companion.' ;;
  esac
  installer_uid=$(/usr/bin/id -u "$installer_user") ||
    installer_fail 'Could not resolve the logged-in user.'
  [[ "$installer_uid" -ge 501 ]] || installer_fail 'A normal logged-in macOS user is required.'
}

as_installer_user() {
  /bin/launchctl asuser "$installer_uid" /usr/bin/sudo -H -u "$installer_user" -- "$@"
}

require_closed_applications() {
  local status
  if /usr/bin/pgrep -x OBS >/dev/null; then
    installer_fail 'Close OBS Studio before installing its Companion plugin.'
  else
    status=$?
    [[ "$status" -eq 1 ]] || installer_fail 'Could not inspect the OBS process.'
  fi
  if /usr/bin/pgrep -f '(^|/)panelive-companion( |$)' >/dev/null; then
    installer_fail 'Quit PaneLive Companion before installing this update.'
  else
    status=$?
    [[ "$status" -eq 1 ]] || installer_fail 'Could not inspect the Companion process.'
  fi
}
