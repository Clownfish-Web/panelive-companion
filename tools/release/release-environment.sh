#!/usr/bin/env bash
# Shared helpers for the Linux and macOS release scripts. Source this file; do not execute it.
#
# It mirrors ReleaseEnvironment.psm1: the same ignored tools/release/.env file, the same variable
# names, and the same precedence (command-line option, process environment, .env value, script
# default). Relative paths resolve from the repository root.

release_supported_environment_names=(
  PANELIVE_COMPANION_VISUAL_STUDIO_RELEASE
  PANELIVE_COMPANION_VISUAL_STUDIO_PATH
  PANELIVE_COMPANION_CMAKE_PATH
  PANELIVE_COMPANION_CTEST_PATH
  PANELIVE_COMPANION_NINJA_PATH
  PANELIVE_COMPANION_VCPKG_ROOT
  PANELIVE_COMPANION_RELEASE_BUILD_DIRECTORY
  PANELIVE_COMPANION_PLUGIN_BUILD_DIRECTORY
  PANELIVE_COMPANION_OBS_QT_DIRECTORY
  PANELIVE_COMPANION_PACKAGE_PARENT
  PANELIVE_COMPANION_PACKAGE_ROOT
  PANELIVE_COMPANION_MACOS_INSTALLER_IDENTITY
  PANELIVE_COMPANION_INSTALLER_OUTPUT_DIRECTORY
  PANELIVE_COMPANION_ISCC_PATH
  PANELIVE_COMPANION_INSTALLER_SIGN_TOOL
  PANELIVE_COMPANION_REQUIRE_SIGNED_BINARIES
  PANELIVE_COMPANION_INSTALL_DIRECTORY
  PANELIVE_COMPANION_VERSION
)

# Indexed storage also works with macOS Bash 3.2. The unused first slot keeps arrays defined
# under nounset on that shell; supported environment names are never empty.
release_file_environment_names=("")
release_file_environment_values=("")

release_file_environment_index() {
  local index
  for ((index = 1; index < ${#release_file_environment_names[@]}; ++index)); do
    if [[ "${release_file_environment_names[index]}" == "$1" ]]; then
      printf '%s' "${index}"
      return 0
    fi
  done
  return 1
}

release_fail() {
  echo "error: $*" >&2
  exit 1
}

release_step() {
  printf '\n==> %s\n' "$*"
}

release_repository_root() {
  cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd
}

release_environment_name_supported() {
  local candidate
  for candidate in "${release_supported_environment_names[@]}"; do
    [[ "${candidate}" == "$1" ]] && return 0
  done
  return 1
}

# release_import_environment_file <path> <required: true|false>
release_import_environment_file() {
  local path="$1" required="$2" line_number=0 line trimmed name value
  if [[ ! -f "${path}" ]]; then
    [[ "${required}" == true ]] && release_fail "Release environment file was not found: ${path}"
    return 0
  fi
  while IFS= read -r line || [[ -n "${line}" ]]; do
    line_number=$((line_number + 1))
    trimmed="${line#"${line%%[![:space:]]*}"}"
    trimmed="${trimmed%"${trimmed##*[![:space:]]}"}"
    [[ -z "${trimmed}" || "${trimmed}" == \#* ]] && continue
    if [[ ! "${trimmed}" =~ ^([A-Z][A-Z0-9_]*)[[:space:]]*=[[:space:]]*(.*)$ ]]; then
      release_fail "Invalid release environment entry at ${path}:${line_number}."
    fi
    name="${BASH_REMATCH[1]}"
    value="${BASH_REMATCH[2]}"
    value="${value%"${value##*[![:space:]]}"}"
    release_environment_name_supported "${name}" ||
      release_fail "Unknown release environment variable '${name}' at ${path}:${line_number}."
    release_file_environment_index "${name}" >/dev/null &&
      release_fail "Duplicate release environment variable '${name}' at ${path}:${line_number}."
    if [[ "${value}" == \"* || "${value}" == \'* || "${value}" == *\" || "${value}" == *\' ]]; then
      if [[ ${#value} -lt 2 || "${value:0:1}" != "${value:${#value}-1:1}" ]]; then
        release_fail "Mismatched quotes for '${name}' at ${path}:${line_number}."
      fi
      value="${value:1:${#value}-2}"
    fi
    release_file_environment_names+=("${name}")
    release_file_environment_values+=("${value}")
  done < "${path}"
}

# release_configured_value <variable name> <current value>
# Prints the command-line value when set, else the process environment, else the .env value.
release_configured_value() {
  local name="$1" current="$2" index
  if [[ -n "${current}" ]]; then
    printf '%s' "${current}"
  elif [[ -n "${!name:-}" ]]; then
    printf '%s' "${!name}"
  elif index="$(release_file_environment_index "${name}")"; then
    printf '%s' "${release_file_environment_values[index]}"
  fi
}

# release_resolve_path <path> <repository root>
release_resolve_path() {
  local path="$1" root="$2"
  [[ "${path}" == /* ]] || path="${root}/${path}"
  if realpath -m "${path}" 2>/dev/null; then
    return 0
  fi
  # BSD realpath lacks -m. Python is already required by the native build toolchain.
  python3 -c 'import pathlib, sys; print(pathlib.Path(sys.argv[1]).resolve())' "${path}"
}

release_boolean() {
  local value="$1" name="$2"
  case "$(printf '%s' "${value}" | tr '[:upper:]' '[:lower:]')" in
    true | 1) printf 'true' ;;
    false | 0) printf 'false' ;;
    *) release_fail "Invalid boolean value '${value}' for ${name}. Expected true, false, 1, or 0." ;;
  esac
}

# release_cmake_cache_value <CMakeCache.txt> <name>
release_cmake_cache_value() {
  local cache="$1" name="$2" entries
  entries="$(grep -E "^${name}:[^=]+=" "${cache}" || true)"
  [[ "$(printf '%s\n' "${entries}" | grep -c .)" -eq 1 ]] ||
    release_fail "The CMake cache does not contain exactly one ${name} entry."
  printf '%s' "${entries#*=}"
}

release_manifest_version() {
  local root="$1" version
  version="$(sed -nE 's/^[[:space:]]*"version-semver"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/p' "${root}/vcpkg.json")"
  [[ "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$ ]] ||
    release_fail "vcpkg.json must declare a valid version-semver."
  printf '%s' "${version}"
}

release_project_version() {
  local version
  version="$(release_manifest_version "$1")" || return 1
  printf '%s' "${version%%-*}"
}

release_prerelease_tag() {
  local version
  version="$(release_manifest_version "$1")" || return 1
  if [[ "${version}" == *-* ]]; then printf '%s' "${version#*-}"; fi
}

release_assert_version_format() {
  [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
    release_fail "Invalid release version '$1'. Expected MAJOR.MINOR.PATCH."
}

release_debian_version() {
  local version="$1"
  if [[ "${version}" == *-* ]]; then
    printf '%s~%s' "${version%%-*}" "${version#*-}"
  else
    printf '%s' "${version}"
  fi
}

release_sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{ print $1 }'
  else
    shasum -a 256 "$1" | awk '{ print $1 }'
  fi
}

# release_assert_equal_hash <built> <packaged> <label>
release_assert_equal_hash() {
  local built packaged
  built="$(release_sha256 "$1")"
  packaged="$(release_sha256 "$2")"
  [[ "${built}" == "${packaged}" ]] ||
    release_fail "$3 package hash does not match the verified build. Built=${built} Packaged=${packaged}"
  echo "$3 SHA-256: ${built}"
}

# release_remove_owned_directory <path> <expected parent>
release_remove_owned_directory() {
  local path="$1" expected_parent="$2" resolved parent
  [[ ! -L "${path}" ]] || release_fail "Refusing to remove a staging symlink: ${path}"
  [[ -e "${path}" ]] || return 0
  resolved="$(release_resolve_path "${path}" "${PWD}")"
  parent="$(dirname "${resolved}")"
  [[ "${parent}" == "$(release_resolve_path "${expected_parent}" "${PWD}")" ]] ||
    release_fail "Refusing to remove unexpected directory: ${resolved}"
  rm -rf "${resolved}"
}

release_assert_no_native_build_process() {
  local active
  active="$(pgrep -a -x 'cmake|ninja|cc1plus|c\+\+|g\+\+|ld|ld\.gold|ld\.lld|cpack' | grep -v "^$$ " || true)"
  [[ -z "${active}" ]] || release_fail "Another native build process is active:"$'\n'"${active}"
}

# release_assert_release_cache <build directory> <vcpkg root>
release_assert_release_cache() {
  local build="$1" vcpkg_root="$2" cache toolchain expected
  cache="${build}/CMakeCache.txt"
  [[ -f "${cache}" && -f "${build}/build.ninja" ]] ||
    release_fail "The configured release tree is missing: ${build}"
  # The presets pass the toolchain from VCPKG_ROOT on every configure, so the cached
  # CMAKE_TOOLCHAIN_FILE text can lag behind; Z_VCPKG_ROOT_DIR records the root vcpkg applied.
  toolchain="$(release_cmake_cache_value "${cache}" Z_VCPKG_ROOT_DIR)"
  [[ "$(realpath -m "${toolchain}")" == "$(realpath -m "${vcpkg_root}")" ]] ||
    release_fail "Unexpected release vcpkg root: ${toolchain} (expected ${vcpkg_root})"
  [[ "$(release_cmake_cache_value "${cache}" VCPKG_TARGET_TRIPLET)" == "x64-linux" ]] ||
    release_fail "The release cache does not use VCPKG_TARGET_TRIPLET=x64-linux."
  [[ "$(release_cmake_cache_value "${cache}" CMAKE_BUILD_TYPE)" == "Release" ]] ||
    release_fail "The release cache does not use CMAKE_BUILD_TYPE=Release."
  [[ "$(release_cmake_cache_value "${cache}" STUDIO_REMOTE_WARNINGS_AS_ERRORS)" == "ON" ]] ||
    release_fail "The release cache does not enable STUDIO_REMOTE_WARNINGS_AS_ERRORS."
}

release_require_command() {
  command -v "$1" >/dev/null 2>&1 || release_fail "$1 was not found. $2"
}
