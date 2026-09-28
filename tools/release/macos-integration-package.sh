#!/usr/bin/env bash
# Called by the package workflow after the application and helper verification gates.
stage_macos_integration() {
  local repository="$1" build="$2" plugin="$3" package="$4" version="$5"
  local identity=panelive-companion-integrations signing bundle
  signing="$(release_cmake_cache_value "$build/CMakeCache.txt" STUDIO_REMOTE_MACOS_CODESIGN_IDENTITY)"
  [[ -n "$signing" && "$signing" != - ]] || release_fail 'A real signing identity is required for the integration component.'
  bundle="$build/installer-tools/$identity.app"
  mkdir -p "$build/installer-tools"
  release_remove_owned_directory "$bundle" "$build/installer-tools"
  mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
  cp "$build/$identity" "$bundle/Contents/MacOS/$identity"
  sed -e "s/@BUILD_VERSION@/$version/g" -e "s/@VERSION@/${version%%-*}/g" \
    "$repository/packaging/macos/installer/integrations/Info.plist.in" > "$bundle/Contents/Info.plist"
  /usr/bin/ditto "$plugin" "$bundle/Contents/Resources/panelive-companion-obs-plugin.plugin"
  /usr/bin/codesign --force --sign "$signing" --identifier "$identity" --options runtime --timestamp "$bundle"
  macos_verify_bundle "$bundle" "$identity" "${macos_architecture}"
  /usr/bin/ditto "$bundle" "$package/installer-tools/$identity.app"
  macos_verify_bundle "$package/installer-tools/$identity.app" "$identity" "${macos_architecture}"
  release_assert_equal_hash "$bundle/Contents/MacOS/$identity" \
    "$package/installer-tools/$identity.app/Contents/MacOS/$identity" 'OBS integration component'
}
