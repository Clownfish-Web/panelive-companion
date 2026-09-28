#!/usr/bin/env bash
# Build the public source trees, run release boundary checks, stage the canonical
# bundles and verify signatures/hashes before creating a ZIP. Installation is separate.
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/release-environment.sh"
source "$(dirname "${BASH_SOURCE[0]}")/macos-release-environment.sh"
repository_root="$(release_repository_root)"
cd "${repository_root}"

usage() {
  cat <<'USAGE'
Usage: tools/release/build-macos-package.sh [options]
  --architecture ARCH           arm64 or x86_64 (default native host)
  --environment-file PATH        Release .env file (default tools/release/.env when present)
  --cmake-path PATH              cmake (default repository tool, then PATH)
  --ninja-path PATH              ninja (default repository tool, then PATH)
  --vcpkg-root PATH              vcpkg checkout (default .buildtools/vcpkg)
  --release-build-directory PATH Release tree (default out/build/macos-<arm64|x64>-release)
  --plugin-build-directory PATH  Helper tree (default out/build/obs-plugin-macos-<arm64|x64>)
  --obs-qt-directory PATH        Matching Qt (default .buildtools/obs-deps-qt6-2026-07-15-universal)
  --package-parent PATH          Staging parent (default out/package)
  --version MAJOR.MINOR.PATCH     Project version (default vcpkg.json; prerelease appended)

Requires configured Release trees. Reuses cached dependencies.
Intel targets on Apple silicon require Rosetta for executable smoke checks.
Build parallelism follows CMAKE_BUILD_PARALLEL_LEVEL (default 4).
Runs public packaging and Mach-O checks; private application suites are not included.
Creates a development ZIP; does not install/restart applications or perform notarization.
USAGE
}

architecture=""
environment_file="" environment_file_specified=false
cmake_path="" ninja_path="" vcpkg_root=""
release_build="" plugin_build="" qt_directory="" package_parent="" version=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h | --help) usage; exit 0 ;;
    --architecture | --environment-file | --cmake-path | --ninja-path | --vcpkg-root | \
    --release-build-directory | --plugin-build-directory | --obs-qt-directory | \
    --package-parent | --version)
      [[ $# -ge 2 && -n "$2" && "$2" != --* ]] || release_fail "$1 requires a value."
      ;;
    *) usage >&2; release_fail "Unknown option: $1" ;;
  esac
  case "$1" in
    --architecture) architecture="$2" ;;
    --environment-file) environment_file="$2"; environment_file_specified=true ;;
    --cmake-path) cmake_path="$2" ;;
    --ninja-path) ninja_path="$2" ;;
    --vcpkg-root) vcpkg_root="$2" ;;
    --release-build-directory) release_build="$2" ;;
    --plugin-build-directory) plugin_build="$2" ;;
    --obs-qt-directory) qt_directory="$2" ;;
    --package-parent) package_parent="$2" ;;
    --version) version="$2" ;;
  esac
  shift 2
done

macos_select_architecture "${architecture}"
echo "Target: ${macos_architecture}; host: ${macos_host_architecture}; test execution: ${macos_verification_mode}"
environment_file="$(release_resolve_path "${environment_file:-tools/release/.env}" "${repository_root}")"
release_import_environment_file "${environment_file}" "${environment_file_specified}"
cmake_path="$(release_configured_value PANELIVE_COMPANION_CMAKE_PATH "${cmake_path}")"
ninja_path="$(release_configured_value PANELIVE_COMPANION_NINJA_PATH "${ninja_path}")"
vcpkg_root="$(release_configured_value PANELIVE_COMPANION_VCPKG_ROOT "${vcpkg_root}")"
release_build="$(release_configured_value PANELIVE_COMPANION_RELEASE_BUILD_DIRECTORY "${release_build}")"
plugin_build="$(release_configured_value PANELIVE_COMPANION_PLUGIN_BUILD_DIRECTORY "${plugin_build}")"
qt_directory="$(release_configured_value PANELIVE_COMPANION_OBS_QT_DIRECTORY "${qt_directory}")"
package_parent="$(release_configured_value PANELIVE_COMPANION_PACKAGE_PARENT "${package_parent}")"
version="$(release_configured_value PANELIVE_COMPANION_VERSION "${version}")"

cmake="$(macos_release_tool "${cmake_path}" cmake "${repository_root}")"
ninja="$(macos_release_tool "${ninja_path}" ninja "${repository_root}")"
export PATH="${repository_root}/.buildtools/bin:$(dirname "${cmake}"):$(dirname "${ninja}"):${PATH}"
/usr/bin/xcrun --find clang >/dev/null
/usr/bin/xcrun --find swiftc >/dev/null
vcpkg_root="$(release_resolve_path "${vcpkg_root:-.buildtools/vcpkg}" "${repository_root}")"
[[ -f "${vcpkg_root}/scripts/buildsystems/vcpkg.cmake" ]] ||
  release_fail "The vcpkg checkout is missing: ${vcpkg_root}"
export VCPKG_ROOT="${vcpkg_root}"
release_build="$(release_resolve_path "${release_build:-out/build/macos-${macos_build_architecture}-release}" "${repository_root}")"
plugin_build="$(release_resolve_path "${plugin_build:-out/build/obs-plugin-macos-${macos_build_architecture}}" "${repository_root}")"
qt_directory="$(release_resolve_path "${qt_directory:-.buildtools/obs-deps-qt6-2026-07-15-universal}" "${repository_root}")"
package_parent="$(release_resolve_path "${package_parent:-out/package}" "${repository_root}")"
[[ -n "${version}" ]] || version="$(release_project_version "${repository_root}")"
release_assert_version_format "${version}"
build_version="$(release_manifest_version "${repository_root}")"
[[ "${version}" == "${build_version%%-*}" ]] || release_fail "Requested version differs from vcpkg.json."
parallel="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
[[ "${parallel}" =~ ^[1-9][0-9]*$ ]] || release_fail "Build parallelism must be a positive integer."

macos_assert_no_native_build_process
macos_assert_build_cache "${release_build}" "${repository_root}" "${ninja}" "${macos_architecture}"
macos_assert_cache_path "${release_build}" CMAKE_TOOLCHAIN_FILE "${vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
macos_assert_cache_value "${release_build}" VCPKG_TARGET_TRIPLET "${macos_triplet}"
macos_assert_cache_value "${release_build}" STUDIO_REMOTE_WARNINGS_AS_ERRORS ON
macos_assert_build_cache "${plugin_build}" "${repository_root}/obs-plugin" "${ninja}" "${macos_architecture}"
macos_assert_cache_path "${plugin_build}" OBS_QT_DIR "${qt_directory}"

macos_assert_team_signing_configured "${release_build}" STUDIO_REMOTE_MACOS_CODESIGN_IDENTITY
macos_assert_team_signing_configured "${plugin_build}" OBS_PLUGIN_CODESIGN_IDENTITY

release_step "Build Companion release"
"${cmake}" --build "${release_build}" --parallel "${parallel}"
release_step "Test native release compatibility gates"
PANELIVE_RELEASE_TEST_BUNDLE="${release_build}/panelive-companion.app" \
  python3 "${repository_root}/tools/release/tests/release_environment_test.py"
python3 "${repository_root}/tools/release/tests/macos_binary_test.py"
release_step "Build OBS helper release"
"${cmake}" --build "${plugin_build}" --parallel "${parallel}"

identity=panelive-companion
plugin_identity=panelive-companion-obs-plugin
built_bundle="${release_build}/${identity}.app"
built_plugin="${plugin_build}/${plugin_identity}.plugin"
obs_app="$(release_cmake_cache_value "${plugin_build}/CMakeCache.txt" OBS_APP_PATH)"
macos_verify_bundle "${built_bundle}" "${identity}" "${macos_architecture}"
macos_verify_bundle "${built_plugin}" "${plugin_identity}" "${macos_architecture}" "${obs_app}"
[[ "$("${built_bundle}/Contents/MacOS/${identity}" --version)" == "${build_version}" ]] ||
  release_fail "Requested package version does not match the built application."
mkdir -p "${package_parent}"
package_name="${identity}-${build_version}-macos-${macos_architecture}"
package_directory="${package_parent}/${package_name}"
release_remove_owned_directory "${package_directory}" "${package_parent}"
release_step "Stage Companion package"
"${cmake}" --install "${release_build}" --prefix "${package_directory}"
release_step "Stage OBS helper package"
"${cmake}" --install "${plugin_build}" --prefix "${package_directory}/obs-plugin"

packaged_bundle="${package_directory}/${identity}.app"
packaged_plugin="${package_directory}/obs-plugin/obs-plugins/${plugin_identity}.plugin"
for required in LICENSE NOTICE THIRD_PARTY_NOTICES.md; do
  [[ -f "${package_directory}/share/${identity}/${required}" ]] ||
    release_fail "Companion package resource is missing: ${required}"
done
for required in Contents/Resources/LICENSE Contents/Resources/locale/en-US.ini; do
  [[ -f "${packaged_plugin}/${required}" ]] || release_fail "Helper resource is missing: ${required}"
done
for required in Install-ObsPlugin.sh Install-ObsPlugin.command INSTALL_OBS_PLUGIN_MACOS.md; do
  [[ -f "${package_directory}/${required}" ]] || release_fail "Installer resource is missing: ${required}"
done
macos_verify_bundle "${packaged_bundle}" "${identity}" "${macos_architecture}"
macos_verify_bundle "${packaged_plugin}" "${plugin_identity}" "${macos_architecture}" "${obs_app}"
release_assert_equal_hash "${built_bundle}/Contents/MacOS/${identity}" \
  "${packaged_bundle}/Contents/MacOS/${identity}" Companion
release_assert_equal_hash "${built_plugin}/Contents/MacOS/${plugin_identity}" \
  "${packaged_plugin}/Contents/MacOS/${plugin_identity}" 'OBS helper'
release_step "Assemble signed OBS integration component"
source "${repository_root}/tools/release/macos-integration-package.sh"
stage_macos_integration "${repository_root}" "${release_build}" "${built_plugin}" \
  "${package_directory}" "${build_version}"
macos_write_build_verification "${package_directory}/share/${identity}/BUILD_VERIFICATION.txt" \
  "${packaged_bundle}/Contents/MacOS/${identity}" \
  "${packaged_plugin}/Contents/MacOS/${plugin_identity}"

release_step "Create canonical package ZIP"
package_archive="${package_directory}.zip"
temporary_archive="${package_directory}.new.zip"
trap 'rm -f -- "${temporary_archive}"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
[[ ! -L "${temporary_archive}" && ! -L "${package_archive}" && ! -L "${package_archive}.sha256" ]] ||
  release_fail "Refusing to replace a package archive symlink."
rm -f -- "${temporary_archive}"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "${package_directory}" "${temporary_archive}"
mv -f -- "${temporary_archive}" "${package_archive}"
archive_hash="$(release_sha256 "${package_archive}")"
printf '%s  %s\n' "${archive_hash}" "${package_name}.zip" > "${package_archive}.sha256"
echo "Package: ${package_directory}"
echo "Package ZIP: ${package_archive}"
echo "Package ZIP SHA-256: ${archive_hash}"
