#!/bin/bash
# Shared placement uses Installer's single approval. User operations drop privileges explicitly.

installer_fail() {
  printf '%s\n' "$1" >&2
  exit 1
}

installer_context() {
  [[ "${1:-}" == / ]] || installer_fail 'Install Companion on the currently running macOS volume.'
  [[ "$EUID" -eq 0 ]] || installer_fail 'Open this package with the macOS Installer application.'
  installer_user=$(/usr/bin/stat -f '%Su' /dev/console) ||
    installer_fail 'Could not identify the logged-in user.'
  case "$installer_user" in
    '' | root | loginwindow | _*) installer_fail 'Log in to macOS before installing Companion.' ;;
  esac
  installer_uid=$(/usr/bin/id -u "$installer_user") ||
    installer_fail 'Could not resolve the logged-in user.'
  [[ "$installer_uid" -ge 501 ]] || installer_fail 'A normal logged-in macOS user is required.'
  if ! as_installer_user /bin/bash -c '
    [[ ! -e "$HOME/Applications/panelive-companion.app" &&
       ! -L "$HOME/Applications/panelive-companion.app" ]]'; then
    installer_fail 'Remove the previous personal Companion app from ~/Applications, keeping your settings, before installing in /Applications.'
  fi
}

as_installer_user() {
  # Root already has authority to change identity. -n forbids a second password prompt.
  /bin/launchctl asuser "$installer_uid" /usr/bin/sudo -n -H -u "$installer_user" -- "$@"
}

install_shared_integration() {
  "$1/installer-tools/panelive-companion-integrations.app/Contents/MacOS/panelive-companion-integrations" --elevated
}

require_closed_applications() {
  local status
  if /usr/bin/pgrep -x OBS >/dev/null; then
    installer_fail 'Close OBS Studio before installing its Companion plugin.'
  else
    status=$?
    [[ "$status" -eq 1 ]] || installer_fail 'Could not inspect the OBS process.'
  fi
  if /usr/bin/pgrep -x panelive-companion >/dev/null; then
    installer_fail 'Quit PaneLive Companion before installing this update.'
  else
    status=$?
    [[ "$status" -eq 1 ]] || installer_fail 'Could not inspect the Companion process.'
  fi
}
