#!/usr/bin/env bash
# Build on the oldest supported userspace, regardless of the host distribution.
set -euo pipefail
repository_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repository_root"
image=${PANELIVE_LINUX_BUILD_IMAGE:-localhost/panelive-linux-release-build:latest}
jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-4}
if [[ ${1:-} == --help ]]; then
  echo 'Usage: tools/release/build-linux-packages.sh [--skip-image-build]'
  echo 'Requires x86_64 Linux, Podman, and network access for the initial build.'
  echo 'Outputs a DEB, CLI installer tar.gz, checksums and provenance under out/release/.'
  exit 0
fi
if [[ ${1:-} != --inside-container ]]; then
  [[ $# == 0 || ( $# == 1 && $1 == --skip-image-build ) ]] || { echo 'Unknown argument' >&2; exit 2; }
  [[ $(uname -m) == x86_64 ]] || { echo 'An x86_64 build host is required.' >&2; exit 1; }
  if [[ ${1:-} != --skip-image-build ]]; then
    # Send only recipe inputs, never the source tree's multi-gigabyte build caches.
    context=$(mktemp -d)
    trap 'rm -rf -- "$context"' EXIT
    mkdir -p "$context/tools/dependencies"
    cp tools/dependencies/linux-jammy{.Containerfile,-obs.list} "$context/tools/dependencies/"
    podman build -f "$context/tools/dependencies/linux-jammy.Containerfile" -t "$image" "$context"
    rm -rf -- "$context"
    trap - EXIT
  fi
  image_id=$(podman image inspect "$image" --format '{{.Id}}')
  exec podman run --rm -v "$repository_root:/workspace" -w /workspace \
    -e "CMAKE_BUILD_PARALLEL_LEVEL=$jobs" -e "PANELIVE_BUILD_IMAGE_ID=$image_id" \
    "$image" bash tools/release/build-linux-packages.sh --inside-container
fi
source /etc/os-release
[[ $ID == ubuntu && $VERSION_ID == 22.04 && $(uname -m) == x86_64 ]] || {
  echo 'Release binaries must be built in Ubuntu 22.04 x86_64 userspace.' >&2; exit 1;
}
version=$(python3 -c 'import json; print(json.load(open("vcpkg.json"))["version-semver"])')
baseline=$(python3 -c 'import json; print(json.load(open("vcpkg.json"))["builtin-baseline"])')
export VCPKG_ROOT=/workspace/.buildtools/vcpkg-jammy
export VCPKG_MAX_CONCURRENCY="$jobs"
mkdir -p .buildtools/vcpkg-portable-cache
export VCPKG_BINARY_SOURCES='clear;files,/workspace/.buildtools/vcpkg-portable-cache,readwrite'
if [[ ! -d $VCPKG_ROOT/.git ]]; then
  git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
  git -C "$VCPKG_ROOT" checkout "$baseline"
fi
[[ $(git -C "$VCPKG_ROOT" rev-parse HEAD) == "$baseline" ]] || {
  echo "Expected vcpkg baseline $baseline; use a matching checkout at $VCPKG_ROOT" >&2; exit 1;
}
if [[ ! -x $VCPKG_ROOT/vcpkg ]]; then bash "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics; fi
build=out/build/linux-x64-jammy-release
cmake --preset linux-x64-release -B "$build" -DCMAKE_INSTALL_LIBDIR=lib -DSTUDIO_REMOTE_LINUX_DISTRIBUTION=DEBIAN
cmake --build "$build" --parallel "$jobs"
# A fresh staging directory guarantees that obsolete files cannot enter a release.
mkdir -p out/package
work=$(mktemp -d "$repository_root/out/package/linux-installers.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
name="panelive-companion-$version-linux-x64"
appdir="$work/$name"
cmake --install "$build" --prefix "$appdir/usr" --strip
rm -f "$appdir/usr/lib/panelive-companion/configure-obs-helper"
rm -rf "$appdir/usr/share/lintian"
cp packaging/linux/launch.sh packaging/linux/configure-obs-helper packaging/linux/uninstall.sh "$appdir/"
cp packaging/linux/README.md "$appdir/README.md"
install -Dm644 packaging/linux/README.md "$appdir/usr/share/panelive-companion/packaging/linux/README.md"
python3 tools/release/bundle-linux-runtime.py "$appdir"
"$appdir/launch.sh" --version
assets="$work/assets"
mkdir -p "$assets"
python3 tools/release/stage-linux-installers.py "$appdir" "$work" "$assets" "$version"
cp packaging/linux/README.md "$assets/linux-INSTALL.md"
python3 tools/release/linux-build-manifest.py "$assets" "$appdir"
(cd "$assets" && sha256sum "$name.deb" "$name-installer.tar.gz" linux-INSTALL.md linux-build-manifest.json > SHA256SUMS-linux)
output="out/release/v$version/linux"
# Preserve superseded artifacts and their matching checksums outside the upload folder.
for previous in "$output" "out/release/v$version/linux-portable"; do
  if [[ -d $previous ]]; then
    mkdir -p "out/archive/v$version"
    archived=$(mktemp -d "out/archive/v$version/$(basename "$previous").XXXXXXXX")
    rmdir "$archived"
    mv -- "$previous" "$archived"
  fi
done
mkdir -p "$output"
cp "$assets/"* "$output/"
(cd "$output" && sha256sum --check SHA256SUMS-linux)
printf '\nRelease assets: %s/%s\n' "$repository_root" "$output"
