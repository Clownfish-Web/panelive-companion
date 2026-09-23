#!/bin/bash
# Native counterpart of the Windows standalone plugin installer. No runtime configuration changes.

obs_is_running() {
  /usr/bin/pgrep -x OBS >/dev/null
}

native_macos_architecture() {
  local architecture
  architecture=$(/usr/bin/uname -m) || return 1
  if [[ "$(/usr/sbin/sysctl -in hw.optional.arm64 2>/dev/null)" == 1 ]]; then
    architecture=arm64
  fi
  case "$architecture" in
    arm64 | x86_64) ;;
    *) printf '%s\n' 'A native Apple silicon or Intel Mac is required.' >&2; return 1 ;;
  esac
  printf '%s\n' "$architecture"
}

verify_obs_compatibility() {
  local obs="$1" plugin="$2" architecture description version binary
  architecture=$(native_macos_architecture) || return 1
  version=$(/usr/bin/sw_vers -productVersion) || return 1
  if [[ ! "$version" =~ ^[0-9]+\. || "${version%%.*}" -lt 13 ]]; then
    printf '%s\n' 'Companion requires macOS 13 or newer.' >&2
    return 1
  fi
  version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
    "$obs/Contents/Info.plist" 2>/dev/null) || {
    printf '%s\n' 'Could not read the OBS Studio version. Reinstall OBS Studio 32.2.x.' >&2
    return 1
  }
  if [[ ! "$version" =~ ^32\.2\.[0-9]+$ ]]; then
    printf '%s\n' 'This helper requires OBS Studio 32.2.x with Qt 6.11.1.' >&2
    return 1
  fi
  version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' \
    "$obs/Contents/Frameworks/QtCore.framework/Resources/Info.plist" 2>/dev/null) || {
    printf '%s\n' 'Could not read the OBS Qt runtime version. Reinstall OBS Studio 32.2.x.' >&2
    return 1
  }
  if [[ "$version" != 6.11.1 ]]; then
    printf '%s\n' 'This helper requires the OBS Qt 6.11.1 runtime.' >&2
    return 1
  fi
  for binary in "$plugin" "$obs/Contents/MacOS/OBS" \
    "$obs/Contents/Frameworks/libobs.framework/libobs" \
    "$obs/Contents/Frameworks/obs-frontend-api.dylib" \
    "$obs/Contents/Frameworks/QtCore.framework/QtCore" \
    "$obs/Contents/Frameworks/QtGui.framework/QtGui" \
    "$obs/Contents/Frameworks/QtWidgets.framework/QtWidgets"; do
    description=$(/usr/bin/file -bL -- "$binary") || return 1
    if [[ "$description" != *Mach-O* ||
          ! "$description" =~ (^|[[:space:]])${architecture}($|[[:space:]]) ]]; then
      printf 'OBS and the helper must contain the native %s architecture: %s\n' \
        "$architecture" "$binary" >&2
      return 1
    fi
  done
}

verify_plugin_bundle() {
  local bundle="$1"
  local identity="panelive-companion-obs-plugin"
  local recorded_identity
  recorded_identity=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' \
    "$bundle/Contents/Info.plist" 2>/dev/null) || return 1
  if [[ "$recorded_identity" != "$identity" ]]; then
    printf '%s\n' 'Packaged OBS plugin has an unexpected bundle identity.' >&2
    return 1
  fi
  /usr/bin/codesign --verify --strict "$bundle"
}

replace_plugin_bundle() (
  # A signed macOS bundle must be replaced as a unit, without resources left from an older build.
  # This subshell owns its private staging directory and rollback through an EXIT trap.
  local source="$1" installed="$2"
  local parent transaction staged previous
  local committed=no placed=no
  parent=$(dirname "$installed")
  transaction=$(/usr/bin/mktemp -d "$parent/.panelive-companion-obs-plugin.XXXXXXXX") || exit 1
  staged="$transaction/panelive-companion-obs-plugin.plugin"
  previous="$transaction/previous/panelive-companion-obs-plugin.plugin"
  finish_plugin_replacement() {
    local result="$1"
    trap - EXIT
    if [[ "$committed" != yes ]]; then
      if [[ "$placed" == yes ]]; then
        /bin/rm -rf -- "$installed" || exit 1
      fi
      if [[ -e "$previous" ]]; then
        if ! /bin/mv -- "$previous" "$installed"; then
          printf '%s\n' "Could not restore the previous plugin; retained at $previous" >&2
          exit 1
        fi
      fi
    fi
    /bin/rm -rf -- "$transaction" || exit 1
    exit "$result"
  }
  trap 'finish_plugin_replacement "$?"' EXIT
  trap 'exit 129' HUP
  trap 'exit 130' INT
  trap 'exit 143' TERM
  /usr/bin/ditto "$source" "$staged" || exit 1
  verify_plugin_bundle "$staged" || exit 1
  if [[ -e "$installed" ]]; then
    /bin/mkdir "$transaction/previous" || exit 1
    /bin/mv -- "$installed" "$previous" || exit 1
  fi
  /bin/mv -- "$staged" "$installed" || exit 1
  placed=yes
  verify_plugin_bundle "$installed" || exit 1
  /usr/bin/cmp -s "$source/Contents/MacOS/panelive-companion-obs-plugin" \
    "$installed/Contents/MacOS/panelive-companion-obs-plugin" || exit 1
  committed=yes
)

install_obs_plugin() {
  local package_directory="$1" obs_directory="$2" application_support="$3"
  local process_status source installed resource
  if obs_is_running; then
    printf '%s\n' 'OBS is running. Close OBS before installing or replacing the plugin.' >&2
    return 1
  else
    process_status=$?
    if [[ "$process_status" -ne 1 ]]; then
      printf '%s\n' 'Could not determine whether OBS is running.' >&2
      return 1
    fi
  fi
  source="$package_directory/obs-plugin/obs-plugins/panelive-companion-obs-plugin.plugin"
  for resource in Contents/MacOS/panelive-companion-obs-plugin \
    Contents/Resources/LICENSE Contents/Resources/locale/en-US.ini; do
    if [[ ! -f "$source/$resource" ]]; then
      printf '%s\n' "Packaged OBS plugin resource was not found: $source/$resource" >&2
      return 1
    fi
  done
  if [[ ! -f "$obs_directory/Contents/MacOS/OBS" ||
        ! -f "$obs_directory/Contents/Frameworks/libobs.framework/libobs" ]]; then
    printf '%s\n' "This does not look like an OBS installation: $obs_directory" >&2
    return 1
  fi
  if ! verify_plugin_bundle "$source"; then
    printf '%s\n' 'Packaged OBS plugin signature or identity is invalid.' >&2
    return 1
  fi
  verify_obs_compatibility "$obs_directory" \
    "$source/Contents/MacOS/panelive-companion-obs-plugin" || return 1
  installed="$application_support/obs-studio/plugins/panelive-companion-obs-plugin.plugin"
  if [[ -L "$installed" || ( -e "$installed" && ! -d "$installed" ) ]]; then
    printf '%s\n' 'The existing OBS plugin destination is not a regular bundle directory.' >&2
    return 1
  fi
  /bin/mkdir -p -- "$(dirname "$installed")" || return 1
  replace_plugin_bundle "$source" "$installed" || return 1
  printf '%s\n' 'panelive-companion-obs-plugin installed successfully.'
}

install_obs_plugin_main() {
  local obs_directory='/Applications/OBS.app'
  local package_directory
  while [[ "$#" -gt 0 ]]; do
    case "$1" in
      --obs-directory)
        if [[ "$#" -lt 2 || -z "$2" ]]; then
          printf '%s\n' '--obs-directory requires an OBS application path.' >&2
          return 1
        fi
        obs_directory="$2"
        shift 2
        ;;
      --help)
        printf '%s\n' 'Usage: Install-ObsPlugin.sh [--obs-directory /Applications/OBS.app]'
        return 0
        ;;
      *)
        printf '%s\n' 'Unsupported plugin installer argument.' >&2
        return 1
        ;;
    esac
  done
  package_directory=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P) || return 1
  install_obs_plugin "$package_directory" "$obs_directory" "${HOME:?}/Library/Application Support"
}

# Sourcing exposes the same named installation operations to packaging verification.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  install_obs_plugin_main "$@"
fi
