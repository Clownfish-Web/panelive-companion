#!/usr/bin/env bash
# Native counterpart of Build-WindowsInstaller.ps1. Package the verified staged release;
# build-macos-package.sh owns native compilation, tests and staging.
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/release-environment.sh"
source "$(dirname "${BASH_SOURCE[0]}")/macos-release-environment.sh"
repository_root="$(release_repository_root)"
cd "${repository_root}"

usage() {
  cat <<'USAGE'
Usage: tools/release/build-macos-installer.sh [options]
  --architecture ARCH           arm64 or x86_64 (default native host)
  --environment-file PATH        Release .env file (default tools/release/.env when present)
  --version MAJOR.MINOR.PATCH     Project version (default vcpkg.json; prerelease appended)
  --release-build-directory PATH Release tree (default out/build/macos-<arm64|x64>-release)
  --plugin-build-directory PATH  Helper tree (default out/build/obs-plugin-macos-<arm64|x64>)
  --package-parent PATH          Staging parent (default out/package)
  --package-root PATH            Staged package (default parent plus canonical versioned name)
  --output-directory PATH        Installer output (default out/installer)
  --signing-identity NAME        Developer ID Installer identity (optional for local evaluation)

Creates an integrated native macOS PKG and SHA-256 manifest from an already staged package.
After source changes, first run tools/release/build-macos-package.sh to rebuild the public source.
Does not rebuild, stop applications, install, re-sign bundles or notarize the package.
USAGE
}

architecture=""
environment_file="" environment_file_specified=false
version="" release_build="" plugin_build="" package_parent="" package_root="" output_directory="" signing_identity=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h | --help) usage; exit 0 ;;
    --architecture | --environment-file | --version | --release-build-directory | --plugin-build-directory | \
    --package-parent | --package-root | --output-directory | --signing-identity)
      [[ $# -ge 2 && -n "$2" && "$2" != --* ]] || release_fail "$1 requires a value."
      ;;
    *) usage >&2; release_fail "Unknown option: $1" ;;
  esac
  case "$1" in
    --architecture) architecture="$2" ;;
    --environment-file) environment_file="$2"; environment_file_specified=true ;;
    --version) version="$2" ;;
    --release-build-directory) release_build="$2" ;;
    --plugin-build-directory) plugin_build="$2" ;;
    --package-parent) package_parent="$2" ;;
    --package-root) package_root="$2" ;;
    --output-directory) output_directory="$2" ;;
    --signing-identity) signing_identity="$2" ;;
  esac
  shift 2
done

macos_select_architecture "${architecture}"
environment_file="$(release_resolve_path "${environment_file:-tools/release/.env}" "${repository_root}")"
release_import_environment_file "${environment_file}" "${environment_file_specified}"
version="$(release_configured_value PANELIVE_COMPANION_VERSION "${version}")"
release_build="$(release_configured_value PANELIVE_COMPANION_RELEASE_BUILD_DIRECTORY "${release_build}")"
plugin_build="$(release_configured_value PANELIVE_COMPANION_PLUGIN_BUILD_DIRECTORY "${plugin_build}")"
package_parent="$(release_configured_value PANELIVE_COMPANION_PACKAGE_PARENT "${package_parent}")"
package_root="$(release_configured_value PANELIVE_COMPANION_PACKAGE_ROOT "${package_root}")"
output_directory="$(release_configured_value PANELIVE_COMPANION_INSTALLER_OUTPUT_DIRECTORY "${output_directory}")"
signing_identity="$(release_configured_value PANELIVE_COMPANION_MACOS_INSTALLER_IDENTITY "${signing_identity}")"
if [[ -n "${signing_identity}" && "${signing_identity}" != "Developer ID Installer: "* ]]; then
  release_fail "Select the full Developer ID Installer certificate name for package signing."
fi

[[ -n "${version}" ]] || version="$(release_project_version "${repository_root}")"
release_assert_version_format "${version}"
build_version="$(release_manifest_version "${repository_root}")"
[[ "${version}" == "${build_version%%-*}" ]] || release_fail "Requested version differs from vcpkg.json."
identity=panelive-companion
plugin_identity=panelive-companion-obs-plugin
package_name="${identity}-${build_version}-macos-${macos_architecture}"
release_build="$(release_resolve_path "${release_build:-out/build/macos-${macos_build_architecture}-release}" "${repository_root}")"
plugin_build="$(release_resolve_path "${plugin_build:-out/build/obs-plugin-macos-${macos_build_architecture}}" "${repository_root}")"
package_parent="$(release_resolve_path "${package_parent:-out/package}" "${repository_root}")"
package_root="$(release_resolve_path "${package_root:-${package_parent}/${package_name}}" "${repository_root}")"
output_directory="$(release_resolve_path "${output_directory:-out/installer}" "${repository_root}")"
[[ -d "${package_root}" ]] ||
  release_fail "The staged package is missing: ${package_root}. Run build-macos-package.sh first."
[[ "$(basename "${package_root}")" == "${package_name}" ]] ||
  release_fail "Package root must use the canonical name '${package_name}': ${package_root}"
macos_assert_no_native_build_process

built_bundle="${release_build}/${identity}.app"
built_plugin="${plugin_build}/${plugin_identity}.plugin"
obs_app="$(release_cmake_cache_value "${plugin_build}/CMakeCache.txt" OBS_APP_PATH)"
plugin_relative="obs-plugin/obs-plugins/${plugin_identity}.plugin"
macos_verify_bundle "${built_bundle}" "${identity}" "${macos_architecture}"
macos_verify_bundle "${built_plugin}" "${plugin_identity}" "${macos_architecture}" "${obs_app}"
[[ "$("${built_bundle}/Contents/MacOS/${identity}" --version)" == "${build_version}" ]] ||
  release_fail "Requested installer version does not match the built application."

verify_payload() {
  local root="$1" bundle="${2:-$1/${identity}.app}" required
  for required in LICENSE NOTICE THIRD_PARTY_NOTICES.md BUILD_VERIFICATION.txt; do
    [[ -f "${root}/share/${identity}/${required}" ]] ||
      release_fail "Companion package resource is missing: ${required}"
  done
  for required in Contents/Resources/LICENSE Contents/Resources/locale/en-US.ini; do
    [[ -f "${root}/${plugin_relative}/${required}" ]] ||
      release_fail "Helper resource is missing: ${required}"
  done
  for required in Install-ObsPlugin.sh Install-ObsPlugin.command INSTALL_OBS_PLUGIN_MACOS.md; do
    [[ -f "${root}/${required}" ]] || release_fail "Installer resource is missing: ${required}"
  done
  [[ -x "${root}/Install-ObsPlugin.command" && -x "${root}/Install-ObsPlugin.sh" ]] ||
    release_fail "The packaged helper installers must be executable."
  macos_verify_bundle "${bundle}" "${identity}" "${macos_architecture}"
  macos_verify_bundle "${root}/${plugin_relative}" "${plugin_identity}" "${macos_architecture}" "${obs_app}"
  local integration="installer-tools/panelive-companion-integrations.app"
  macos_verify_bundle "${root}/${integration}" panelive-companion-integrations "${macos_architecture}"
  local embedded="${root}/${integration}/Contents/Resources/${plugin_identity}.plugin"
  macos_verify_bundle "${embedded}" "${plugin_identity}" "${macos_architecture}" "${obs_app}"
  release_assert_equal_hash "${built_plugin}/Contents/MacOS/${plugin_identity}" \
    "${embedded}/Contents/MacOS/${plugin_identity}" 'Integration helper payload'
  release_assert_equal_hash \
    "${release_build}/${integration}/Contents/MacOS/panelive-companion-integrations" \
    "${root}/${integration}/Contents/MacOS/panelive-companion-integrations" 'OBS integration component'
  release_assert_equal_hash "${built_bundle}/Contents/MacOS/${identity}" \
    "${bundle}/Contents/MacOS/${identity}" Companion
  release_assert_equal_hash "${built_plugin}/Contents/MacOS/${plugin_identity}" \
    "${root}/${plugin_relative}/Contents/MacOS/${plugin_identity}" 'OBS helper'
}

release_step "Verify staged installer inputs"
verify_payload "${package_root}"
mkdir -p "${output_directory}"
installer="${output_directory}/${package_name}.pkg"
[[ ! -L "${installer}" && ! -L "${installer}.sha256" ]] ||
  release_fail "Refusing to replace an installer or hash manifest symlink."
"${built_bundle}/Contents/MacOS/${identity}" --help | /usr/bin/grep -q -- --initialize-installation ||
  release_fail "Rebuild Companion: this binary does not support integrated installation."
workspace="$(mktemp -d "${output_directory}/.macos-installer.XXXXXXXX")"
trap 'rm -rf -- "${workspace}"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

release_step "Stage integrated installer payload and scripts"
support_relative="Library/Application Support/${identity}/installation"
payload="${workspace}/payload"
mkdir -p "${payload}/Applications" "${workspace}/resources"
/usr/bin/ditto "${package_root}" "${payload}/${support_relative}"
# Shared package resources are readable; user runtime data is never part of this payload.
chmod 0755 "${payload}/Library" "${payload}/Library/Application Support" \
  "${payload}/Library/Application Support/${identity}"

mv "${payload}/${support_relative}/${identity}.app" "${payload}/Applications/${identity}.app"
cp "${repository_root}/packaging/macos/INSTALL_MACOS.txt" "${workspace}/resources/"
cp "${repository_root}/LICENSE" "${workspace}/resources/"
cp "${repository_root}/packaging/macos/INSTALL_MACOS.txt" "${payload}/${support_relative}/"
/usr/bin/ditto "${repository_root}/packaging/macos/installer/scripts" "${workspace}/scripts"
# The executable's signature seals its outer bundle resources. Preserve that bundle for preflight.
/usr/bin/ditto "${built_bundle}" "${workspace}/scripts/${identity}.app"
/usr/bin/ditto "${built_plugin}" "${workspace}/scripts/${plugin_identity}.plugin"
cp "${package_root}/Install-ObsPlugin.sh" "${workspace}/scripts/Install-ObsPlugin.sh"
for script in preinstall postinstall installer-environment.sh Install-ObsPlugin.sh; do
  /bin/bash -n "${workspace}/scripts/${script}"
done

# Fixed locations and whole-bundle upgrades prevent relocation to a developer checkout or
# retaining obsolete signed resources. LocalSystem placement assigns root ownership.
python3 - "${workspace}/components.plist" <<'PYCOMPONENTS'
import plistlib
import sys
components = []
for path in (
    "Applications/panelive-companion.app",
    "Library/Application Support/panelive-companion/installation/obs-plugin/obs-plugins/"
    "panelive-companion-obs-plugin.plugin",
    "Library/Application Support/panelive-companion/installation/installer-tools/"
    "panelive-companion-integrations.app",
):
    components.append(dict(RootRelativeBundlePath=path, BundleIsRelocatable=False,
                           BundleIsVersionChecked=False, BundleHasStrictIdentifier=True,
                           BundleOverwriteAction="upgrade"))
with open(sys.argv[1], "wb") as output:
    plistlib.dump(components, output)
PYCOMPONENTS

release_step "Compile native macOS installer"
# The distribution must name only the architecture present in the validated payload.
sed "s/@MACOS_ARCHITECTURE@/${macos_architecture}/g" \
  "${repository_root}/packaging/macos/installer/distribution.xml.in" > "${workspace}/distribution.xml"
/usr/bin/pkgbuild --root "${payload}" --component-plist "${workspace}/components.plist" \
  --scripts "${workspace}/scripts" --identifier "${identity}" --version "${build_version}" \
  --install-location / "${workspace}/${identity}.pkg"
product_arguments=(--distribution "${workspace}/distribution.xml"
  --resources "${workspace}/resources" --package-path "${workspace}"
  --identifier "${identity}" --version "${build_version}")
if [[ -n "${signing_identity}" ]]; then
  product_arguments+=(--sign "${signing_identity}" --timestamp)
fi
/usr/bin/productbuild "${product_arguments[@]}" "${workspace}/${package_name}.pkg"
if [[ -n "${signing_identity}" ]]; then
  /usr/sbin/pkgutil --check-signature "${workspace}/${package_name}.pkg" > "${workspace}/signature.txt"
  cat "${workspace}/signature.txt"
  python3 - "${workspace}/signature.txt" "${signing_identity}" <<'PYSIGNATURE'
from pathlib import Path
import sys
lines = Path(sys.argv[1]).read_text().splitlines()
if f"1. {sys.argv[2]}" not in [line.strip() for line in lines]:
    raise SystemExit("Installer signature does not match the requested Developer ID identity.")
PYSIGNATURE
fi

release_step "Verify expanded installer payload and install scripts"
/usr/sbin/pkgutil --expand-full "${workspace}/${package_name}.pkg" "${workspace}/expanded"
expanded="${workspace}/expanded/${identity}.pkg"
verify_payload "${expanded}/Payload/${support_relative}" \
  "${expanded}/Payload/Applications/${identity}.app"
cmp "${package_root}/share/${identity}/BUILD_VERIFICATION.txt" \
  "${expanded}/Payload/${support_relative}/share/${identity}/BUILD_VERIFICATION.txt"
for script in preinstall postinstall installer-environment.sh Install-ObsPlugin.sh; do
  cmp "${workspace}/scripts/${script}" "${expanded}/Scripts/${script}"
done
macos_verify_bundle "${expanded}/Scripts/${plugin_identity}.plugin" \
  "${plugin_identity}" "${macos_architecture}" "${obs_app}"
preflight_bundle="${expanded}/Scripts/${identity}.app"
macos_verify_bundle "${preflight_bundle}" "${identity}" "${macos_architecture}"
release_assert_equal_hash "${built_bundle}/Contents/MacOS/${identity}" \
  "${preflight_bundle}/Contents/MacOS/${identity}" 'Installer preflight'
[[ "$("${preflight_bundle}/Contents/MacOS/${identity}" --version)" == "${build_version}" ]] ||
  release_fail "The extracted installer preflight application could not report its version."
if ! /usr/sbin/installer -showChoicesXML -pkg "${workspace}/${package_name}.pkg" -target / \
    > "${workspace}/choices.plist"; then
  cat "${workspace}/choices.plist" >&2
  release_fail "macOS Installer could not read the package choices."
fi
/usr/sbin/installer -dominfo -pkg "${workspace}/${package_name}.pkg" > "${workspace}/domains.txt"
python3 - "${workspace}/domains.txt" <<'PYDOMAINS'
import sys
from pathlib import Path
domains = Path(sys.argv[1]).read_text()
if "LocalSystem" not in domains or "CurrentUserHomeDirectory" in domains:
    raise SystemExit("The package must install the app and shared helper with one LocalSystem authorization.")
PYDOMAINS
/usr/bin/plutil -lint "${workspace}/choices.plist"
python3 - "${workspace}/choices.plist" <<'PYCHOICES'
import plistlib
import sys

with open(sys.argv[1], "rb") as source:
    pending = list(plistlib.load(source))
required = []
while pending:
    choice = pending.pop()
    pending.extend(choice.get("childItems", []))
    if choice.get("choiceIdentifier") == "panelive-companion":
        required.append(choice)
if (len(required) != 1 or not required[0].get("choiceIsSelected")
        or required[0].get("choiceIsEnabled") is not False):
    raise SystemExit("The integrated package, including Teleprompter, must be selected and locked.")
PYCHOICES
installer_hash="$(release_sha256 "${workspace}/${package_name}.pkg")"
printf '%s  %s\n' "${installer_hash}" "${package_name}.pkg" > "${workspace}/installer.sha256"
mv -f -- "${workspace}/${package_name}.pkg" "${installer}"
mv -f -- "${workspace}/installer.sha256" "${installer}.sha256"
echo "Installer: ${installer}"
echo "Installer SHA-256: ${installer_hash}"
echo "Hash manifest: ${installer}.sha256"
