#!/usr/bin/env bash
# Native tool/cache boundaries for build-macos-package.sh. Shared release policy stays in
# release-environment.sh, corresponding to ReleaseEnvironment.psm1 on Windows.

macos_select_architecture() {
  [[ "$(uname -s)" == Darwin ]] || release_fail "This workflow requires macOS."
  [[ "$(/usr/sbin/sysctl -in sysctl.proc_translated 2>/dev/null || true)" != 1 ]] ||
    release_fail "Run the release workflow natively, outside Rosetta."
  macos_host_architecture="$(uname -m)"
  macos_architecture="${1:-${macos_host_architecture}}"
  case "${macos_architecture}" in
    arm64) macos_build_architecture=arm64; macos_triplet=arm64-osx ;;
    x86_64) macos_build_architecture=x64; macos_triplet=x64-osx ;;
    *) release_fail "Select arm64 or baseline x86_64." ;;
  esac
  macos_verification_mode=native
  if [[ "${macos_host_architecture}" != "${macos_architecture}" ]]; then
    [[ "${macos_host_architecture}" == arm64 && "${macos_architecture}" == x86_64 ]] ||
      release_fail "This host cannot execute the selected architecture's release tests."
    [[ "$(/usr/bin/arch -x86_64 /usr/sbin/sysctl -in sysctl.proc_translated 2>/dev/null || true)" == 1 ]] ||
      release_fail "Intel tests on Apple silicon require Rosetta. Install Rosetta before packaging."
    macos_verification_mode=rosetta
  fi
}

macos_write_build_verification() {
  local destination="$1" companion="$2" helper="$3"
  cat > "${destination}" <<EOF
Target architecture: ${macos_architecture}
Minimum macOS: 13.0
Build host architecture: ${macos_host_architecture}
Build host macOS: $(/usr/bin/sw_vers -productVersion)
Target test execution: ${macos_verification_mode}
Public release boundary tests: passed
Private application and helper test suites: not included in public source
Application smoke check: --version passed
Release boundary and Mach-O checks: passed
Companion SHA-256: $(release_sha256 "${companion}")
OBS helper SHA-256: $(release_sha256 "${helper}")

Rosetta execution is translated testing on Apple silicon, not native Intel hardware testing.
The deployment target and static audit do not establish runtime acceptance on an older OS.
EOF
}

macos_assert_cache_value() {
  local actual
  actual="$(release_cmake_cache_value "$1/CMakeCache.txt" "$2")"
  [[ "${actual}" == "$3" ]] ||
    release_fail "Unexpected $2 in $1: ${actual} (expected $3)"
}

# CMake may cache a Homebrew symlink while tool discovery resolves its target.
macos_assert_cache_path() {
  local actual expected
  actual="$(release_cmake_cache_value "$1/CMakeCache.txt" "$2")"
  actual="$(release_resolve_path "${actual}" "${PWD}")"
  expected="$(release_resolve_path "$3" "${PWD}")"
  [[ "${actual}" == "${expected}" ]] ||
    release_fail "Unexpected $2 in $1: ${actual} (expected ${expected})"
}

macos_assert_build_cache() {
  local build="$1" source="$2" ninja="$3" architecture="$4"
  [[ -f "${build}/CMakeCache.txt" && -f "${build}/build.ninja" ]] ||
    release_fail "The configured native build tree is missing: ${build}. See docs/MACOS_RELEASE.md."
  macos_assert_cache_value "${build}" CMAKE_HOME_DIRECTORY "${source}"
  macos_assert_cache_value "${build}" CMAKE_BUILD_TYPE Release
  macos_assert_cache_value "${build}" CMAKE_OSX_ARCHITECTURES "${architecture}"
  macos_assert_cache_value "${build}" CMAKE_OSX_DEPLOYMENT_TARGET 13.0
  macos_assert_cache_path "${build}" CMAKE_MAKE_PROGRAM "${ninja}"
}

macos_assert_no_native_build_process() {
  local active status
  if active="$(/usr/bin/pgrep -x 'cmake|ninja|clang|clang\+\+|ld|vcpkg|cpack')"; then
    release_fail "Another native build process is active (PIDs): ${active}"
  else
    status=$?
    [[ "${status}" -eq 1 ]] || release_fail "Could not inspect native build processes."
  fi
}

macos_assert_companion_stopped() {
  local active status
  if active="$(/usr/bin/pgrep -f '(^|/)panelive-companion( |$)')"; then
    release_fail "Close Companion before building or replacing its package (PIDs): ${active}"
  else
    status=$?
    [[ "${status}" -eq 1 ]] || release_fail "Could not inspect Companion processes."
  fi
}

macos_release_tool() {
  local requested="$1" name="$2" root="$3" selected
  if [[ -n "${requested}" ]]; then
    if [[ "${requested}" == */* ]]; then
      selected="$(release_resolve_path "${requested}" "${root}")"
    else
      selected="$(command -v "${requested}" || true)"
    fi
  elif [[ -x "${root}/.buildtools/python/bin/${name}" ]]; then
    selected="${root}/.buildtools/python/bin/${name}"
  else
    selected="$(command -v "${name}" || true)"
  fi
  [[ -n "${selected}" && -x "${selected}" ]] ||
    release_fail "Required build tool is missing: ${requested:-${name}}"
  release_resolve_path "${selected}" "${root}"
}

macos_assert_team_signing_configured() {
  local signing
  signing="$(release_cmake_cache_value "$1/CMakeCache.txt" "$2")"
  [[ -n "$signing" && "$signing" != - ]] ||
    release_fail "Configure an Apple-issued signing identity in $2 before packaging; ad-hoc updates break Keychain trust."
}

macos_verify_team_signature() {
  # Integrity alone accepts ad-hoc signatures, whose identity changes on every rebuild.
  /usr/bin/codesign --verify --strict --test-requirement \
    '=anchor apple generic and certificate leaf[subject.OU] exists' "$1" ||
    release_fail "An Apple-issued team signature is required for installable code: $1"
}

macos_verify_bundle() {
  local bundle="$1" identity="$2" architecture="$3" actual
  local audit_arguments=(--architecture "${architecture}")
  [[ -f "${bundle}/Contents/MacOS/${identity}" ]] ||
    release_fail "Canonical executable is missing from ${bundle}"
  actual="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "${bundle}/Contents/Info.plist")"
  [[ "${actual}" == "${identity}" ]] || release_fail "Unexpected bundle identity: ${actual}"
  actual="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "${bundle}/Contents/Info.plist")"
  [[ "${actual}" == "${identity}" ]] || release_fail "Unexpected executable identity: ${actual}"
  actual="$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "${bundle}/Contents/Info.plist")"
  [[ "${actual}" == 13.0 ]] || release_fail "Unexpected bundle deployment target: ${actual}"
  if [[ -n "${4:-}" ]]; then
    audit_arguments+=(--obs-app "$4")
  fi
  python3 "$(dirname "${BASH_SOURCE[0]}")/verify-macos-binary.py" \
    "${bundle}/Contents/MacOS/${identity}" "${audit_arguments[@]}" >/dev/null
  macos_verify_team_signature "${bundle}"
}
