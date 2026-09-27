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

register_plugin_link() {
  local shared="$1" installed="$2"
  if [[ -L "$installed" && "$(/usr/bin/readlink "$installed")" == "$shared" ]]; then
    return 0
  fi
  if [[ -e "$installed" || -L "$installed" ]]; then
    printf '%s\n' 'Remove the previous user OBS helper before registering the shared helper.' >&2
    return 1
  fi
  /bin/mkdir -p -- "$(dirname "$installed")" || return 1
  /bin/ln -s -- "$shared" "$installed"
}

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
  local shared='/Library/Application Support/obs-studio/plugins/panelive-companion-obs-plugin.plugin'
  local integration="$package_directory/installer-tools/panelive-companion-integrations.app"
  # Reject a conflicting user bundle before any shared mutation; no development migration shim.
  if [[ -e "$installed" || -L "$installed" ]]; then
    if [[ ! -L "$installed" || "$(/usr/bin/readlink "$installed")" != "$shared" ]]; then
      printf '%s\n' 'Remove the previous user OBS helper before installing the shared helper.' >&2
      return 1
    fi
  fi
  /usr/bin/codesign --verify --strict "$integration" || return 1
  "$integration/Contents/MacOS/panelive-companion-integrations" --install || return 1
  verify_plugin_bundle "$shared" || return 1
  register_plugin_link "$shared" "$installed" || return 1
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
  if [[ "$EUID" -eq 0 ]]; then
    printf '%s\n' 'Run the installer as your normal user; only the shared OBS update may request elevation.' >&2
    return 1
  fi
  package_directory=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P) || return 1
  install_obs_plugin "$package_directory" "$obs_directory" "${HOME:?}/Library/Application Support"
}

# Sourcing exposes the same named installation operations to packaging verification.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  install_obs_plugin_main "$@"
fi
