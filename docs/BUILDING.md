# Build an evaluation copy

Build on the target operating system, from the repository root. These instructions
produce local evaluation packages, not signed/notarized official releases. Use separate
build trees per platform and architecture. The source version is `0.1.0-beta.1`, pinned in `vcpkg.json` and read by both
Companion and OBS helper builds.
Private test harnesses and release automation are intentionally absent from this tree.

## Shared inputs

- Git, CMake **3.28+**, Ninja, and a C++20 compiler. Use a checkout path without
  spaces: the FFmpeg overlay rejects source paths containing spaces.
- vcpkg, using the `builtin-baseline` committed in `vcpkg.json`. Keep all checked-in
  `cmake/overlays` ports: FFmpeg defines the expected version and Windows DLL names,
  while CivetWeb and libdatachannel include required input-validation patches.
- OBS Studio and matching OBS development inputs for the separate helper.
- An internet connection for the initial dependency downloads. Cold builds can take
  substantially longer than subsequent builds.

Clone the repository:

```sh
git clone https://github.com/Clownfish-Web/panelive-companion.git panelive-companion
cd panelive-companion
```

Never copy another machine's CMake cache or installed dependency tree into the clone.
The committed generated protocol files are build inputs; no private generator is needed.

## Windows x64

Use Visual Studio 2022 with Desktop development with C++, a Windows SDK, CMake and
Ninja. Open **Developer PowerShell for VS 2022**, targeting x64. Verify that `cl`,
`dumpbin`, `lib`, `rc`, `mt`, `cmake` and `ninja` are available there.

The helper currently requires **Qt 6.11.1 exactly** and matching OBS 32.2.x inputs.
The private project's Windows baseline was OBS **32.2.1**. An arbitrary current Qt
installation is not a substitute for the Qt package used by OBS.

```powershell
git clone https://github.com/microsoft/vcpkg .buildtools/vcpkg
$baseline = (Get-Content vcpkg.json -Raw | ConvertFrom-Json).'builtin-baseline'
git -C .buildtools/vcpkg checkout $baseline
& ./.buildtools/vcpkg/bootstrap-vcpkg.bat -disableMetrics
$env:VCPKG_ROOT = (Resolve-Path .buildtools/vcpkg).Path
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/dependencies/Acquire-OmtSdk.ps1
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release --parallel 4
```

The OMT helper downloads a pinned, hash-checked MIT-licensed dependency for vMix audio.
CMake downloads pinned Windows App SDK development packages and stages the matching
Windows App Runtime installer. Streamlabs is enabled by this release preset. NDI is
optional and off by default; its SDK/runtime are not redistributed by this repository.

Build the OBS helper separately. Install OBS x64 first. Obtain the matching Qt
Windows x64 development archive from [OBS dependencies](https://github.com/obsproject/obs-deps/releases),
using the version and checksum declared in the matching OBS source checkout, and
extract it under `.buildtools/obs-qt`. `OBS_QT_DIR` below must contain
`lib/cmake/Qt6/Qt6Config.cmake`.

```powershell
git clone --branch 32.2.1 --depth 1 https://github.com/obsproject/obs-studio .buildtools/obs-studio
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/dev/generate_obs_import_libraries.ps1 `
  -ObsBinaryDirectory 'C:\Program Files\obs-studio\bin\64bit' `
  -OutputDirectory '.buildtools/obs-import-libs'
cmake -S obs-plugin -B out/build/obs-plugin-windows-x64 -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DOBS_SOURCE_DIR="$PWD/.buildtools/obs-studio" `
  -DOBS_IMPORT_LIBRARY_DIR="$PWD/.buildtools/obs-import-libs" `
  -DOBS_QT_DIR="$PWD/.buildtools/obs-qt"
cmake --build out/build/obs-plugin-windows-x64 --parallel 4
$package = "$PWD/out/package/panelive-companion-0.1.0-beta.1-windows-x64-custom"
cmake --install out/build/windows-x64-release --prefix $package
cmake --install out/build/obs-plugin-windows-x64 --prefix "$package/obs-plugin"
```

Use a new staging directory when rebuilding a package to avoid retaining obsolete files.
The result includes `bin/panelive-companion.exe`, dependency DLLs,
`redist/WindowsAppRuntimeInstall-x64.exe`, and the helper under
`obs-plugin/obs-plugins/64bit/`. See [running instructions](RUNNING.md#windows).
The plugin DLL is `panelive-companion-obs-plugin.dll`; OBS displays it as
**PaneLive Companion**. Always package the Companion and plugin from the same source
version so their IPC identities match.

To build a Windows setup executable, install Inno Setup 7 and compile the supplied
installer script against that complete staging directory:

```powershell
& "$env:LOCALAPPDATA/Programs/Inno Setup 7/ISCC.exe" `
  "/DPackageRoot=$package" "/DAppVersion=0.1.0-beta.1" `
  "/DInstallerOutputDir=$PWD/out/installer" packaging/windows/panelive-companion.iss
```

Adjust the compiler path if Inno Setup is installed elsewhere. This produces an
unsigned evaluation installer unless a signing tool is explicitly configured.
The main script first compiles the small native integration component into
`$package/installer-tools/`; both setup and uninstall use it for OBS and firewall
changes without a command shell. Inno Setup 7 is also needed to package this
component in an evaluation ZIP. To compile only that component, use the same
`PackageRoot` and `AppVersion` arguments with
`packaging/windows/panelive-companion-integrations.iss`.

After compiling it, create the evaluation ZIP:

```powershell
Compress-Archive -LiteralPath $package -DestinationPath "$package.zip"
Get-FileHash -LiteralPath "$package.zip" -Algorithm SHA256
```

Windows releases always name the combined installer and standalone ZIP checksum
manifest `SHA256SUMS-windows`.

## macOS arm64 or x86_64

Use macOS 13+, Xcode with the Swift 6 toolchain, the Command Line Tools, CMake 3.28+,
Ninja, Git, Python 3, `pkg-config`, `nasm` and `autoconf`/`automake`/`libtool` as needed by vcpkg.
Build natively for your Mac first. Install matching OBS; the recorded helper inputs
are OBS **32.2.2**, **Qt 6.11.1**, and OBS dependency archives **2026-07-15**.

```sh
git clone https://github.com/microsoft/vcpkg .buildtools/vcpkg
baseline=$(python3 -c 'import json; print(json.load(open("vcpkg.json"))["builtin-baseline"])')
git -C .buildtools/vcpkg checkout "$baseline"
bash .buildtools/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT="$PWD/.buildtools/vcpkg"
cmake --preset macos-arm64-release
cmake --build --preset macos-arm64-release --parallel 4
```

For Intel, replace `macos-arm64-release` with `macos-x64-release` throughout and set
`CMAKE_OSX_ARCHITECTURES=x86_64` for the helper. OBS, Qt, the helper and Companion must
all have the required architecture. This is not a Universal 2 build.

Obtain `macos-deps-2026-07-15-universal.tar.xz` and
`macos-deps-qt6-2026-07-15-universal.tar.xz` from
[OBS dependencies](https://github.com/obsproject/obs-deps/releases/tag/2026-07-15).
Verify their hashes against the matching OBS source's dependency metadata before
extracting to `.buildtools/obs-deps` and `.buildtools/obs-qt`. These directories must
contain `include/simde/x86/sse2.h` and `lib/cmake/Qt6/Qt6Config.cmake`, respectively.

```sh
git clone --branch 32.2.2 --depth 1 https://github.com/obsproject/obs-studio .buildtools/obs-studio
cmake -S obs-plugin -B out/build/obs-plugin-macos-arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DOBS_SOURCE_DIR="$PWD/.buildtools/obs-studio" \
  -DOBS_DEPENDENCIES_DIR="$PWD/.buildtools/obs-deps" \
  -DOBS_QT_DIR="$PWD/.buildtools/obs-qt" \
  -DOBS_APP_PATH=/Applications/OBS.app
cmake --build out/build/obs-plugin-macos-arm64 --parallel 4
package="$PWD/out/package/panelive-companion-0.1.0-beta.1-macos-arm64-custom"
cmake --install out/build/macos-arm64-release --prefix "$package"
cmake --install out/build/obs-plugin-macos-arm64 --prefix "$package/obs-plugin"
```

The Companion is `panelive-companion.app`; the separate helper is
`obs-plugin/obs-plugins/panelive-companion-obs-plugin.plugin`. CMake signs both
locally with an ad-hoc signature by default. To use your own identity, configure
`STUDIO_REMOTE_MACOS_CODESIGN_IDENTITY` for Companion and `OBS_PLUGIN_CODESIGN_IDENTITY`
for the helper. Local signatures do not establish notarization or release acceptance.
See [running instructions](RUNNING.md#macos).

The standalone helper installer now uses a separate native integration bundle to
update the shared OBS helper. Before creating the ZIP, stage that bundle with the
matching helper. Run from the repository root, keeping `$package` from above:

```sh
integration="$package/installer-tools/panelive-companion-integrations.app"
mkdir -p "$integration/Contents/MacOS" "$integration/Contents/Resources"
cp out/build/macos-arm64-release/panelive-companion-integrations \
  "$integration/Contents/MacOS/panelive-companion-integrations"
sed -e 's/@BUILD_VERSION@/0.1.0-beta.1/g' -e 's/@VERSION@/0.1.0/g' \
  packaging/macos/installer/integrations/Info.plist.in > "$integration/Contents/Info.plist"
ditto "$package/obs-plugin/obs-plugins/panelive-companion-obs-plugin.plugin" \
  "$integration/Contents/Resources/panelive-companion-obs-plugin.plugin"
codesign --force --sign - --identifier panelive-companion-integrations \
  --options runtime --timestamp=none "$integration"
codesign --verify --strict "$integration"
ditto -c -k --keepParent "$package" "$package.zip"
shasum -a 256 "$package.zip"
```

For Intel, use `macos-x64-release` in the executable copy command. The ad-hoc signature
above is for local evaluation. For a signed build, use your own signing identity and
the appropriate timestamp option, consistent with the Companion and helper builds.

For the complete signed staging workflow, integrated `.pkg` installers, Intel builds,
and GitHub release assets, follow [macOS release packaging](MACOS_RELEASE.md).

## Linux x86_64 (Ubuntu and Debian)

For release installers with bundled application libraries, use the
[Linux release workflow](LINUX_RELEASE.md). The commands below produce a native,
system-library-dependent `.deb` for source-build users.

Ubuntu 22.04 with Linux 5.15, glibc 2.35, GTK 4.6 and Libadwaita 1.1 is the declared
support floor. The same baseline-built Companion installer has passed package and
desktop/runtime checks on Ubuntu 22.04, 24.04 and 26.04, plus Debian 12 and 13.
See [Linux support and validation](LINUX_SUPPORT.md) for the exact test scope and
OBS helper requirements. Build distributable binaries in Ubuntu
22.04 userspace: changing package dependencies cannot lower a newer binary's runtime
requirements. A container verifies userspace; kernel-floor validation needs a Linux
5.15 VM.

Use a native Linux checkout and CMake 3.28+ (22.04's default CMake is too old).
The helper requires native OBS 30 or newer with Qt 6 and links the OBS 30 shared-library
ABI. OBS must supply matching development headers and both `libobs` and
`obs-frontend-api` CMake configurations. Ubuntu 22.04's stock OBS 27 and Debian 12's
stock OBS 29.0 are too old for the helper. The baseline build uses OBS 30.2.3 from
the official OBS PPA, whose package includes its development headers and replaces
the stock `libobs-dev`/`libobs0`. Do not mix headers and libraries from different OBS
releases. Flatpak/Snap OBS cannot load the bundled native helper.

The [Ubuntu 22.04 build recipe](../tools/dependencies/linux-jammy.Containerfile)
pins the base image, authenticates the OBS PPA, selects the baseline OBS SDK and
verifies the downloaded CMake archive. With Podman installed, run from the repository
root:

```sh
podman build -f tools/dependencies/linux-jammy.Containerfile -t panelive-jammy-build .
podman run --rm -it -v "$PWD:/workspace" panelive-jammy-build bash
```

Inside this container, continue with the vcpkg/build commands below. Keep the mount
path and compiler consistent for a configured tree. Use separate build and installed
dependency trees for Ubuntu 22.04 and 24.04; do not run concurrent vcpkg operations
against one checkout. Record `dpkg-query -W` with release evidence because apt security
updates can change the environment even with pinned build inputs.

For a native build, install these dependencies and the matching OBS SDK first. The
container already supplies them; omit `libobs-dev` when using the PPA package above:

```sh
sudo apt-get update
sudo apt-get install --yes \
  build-essential ca-certificates cmake curl git dpkg-dev fakeroot \
  libadwaita-1-dev libavahi-client-dev libdbus-1-dev libglib2.0-dev \
  libgtk-4-dev libsecret-1-dev libjpeg-dev libobs-dev libpulse-dev librsvg2-common \
  nasm ninja-build pkg-config qt6-base-dev tar unzip zip python3 \
  autoconf automake libtool
```

Then bootstrap a Linux-local vcpkg checkout and build:

```sh
git clone https://github.com/microsoft/vcpkg .buildtools/vcpkg
baseline=$(python3 -c 'import json; print(json.load(open("vcpkg.json"))["builtin-baseline"])')
git -C .buildtools/vcpkg checkout "$baseline"
bash .buildtools/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT="$PWD/.buildtools/vcpkg"
cmake --preset linux-x64-release
cmake --build --preset linux-x64-release --parallel 4
cpack --config out/build/linux-x64-release/CPackConfig.cmake -G DEB
cd out/package/linux
sha256sum --check SHA256SUMS-linux
```

Linux builds the OBS helper as part of the top-level build. CPack writes one installer,
`out/package/linux/panelive-companion-0.1.0-beta.1-linux-x64.deb`, and its checksum file,
`SHA256SUMS-linux`. Debian package metadata retains the version `0.1.0~beta.1` and
the package name `panelive-companion` for upgrade compatibility.

The package includes the helper in a private directory. Maintainer scripts activate
it for compatible native OBS, and APT file triggers recheck setup when OBS or its
libraries change. `dpkg-shlibdeps` derives Companion's required dependencies and the
helper's optional integration dependencies separately. The installer checks OBS's
version, its `libobs.so.30` ABI and the helper's library/symbol resolution before
exposing the helper to OBS. Standalone teleprompter use does not require OBS.

The distro's libjpeg archive is linked statically to avoid Ubuntu/Debian SONAME
differences; its license and the vcpkg dependency SPDX inventories are included.
For an unpacked staging tree containing both payloads instead:

```sh
cmake --install out/build/linux-x64-release \
  --prefix "$PWD/out/package/linux-custom/usr"
```

Run the staging command from the repository root. A staged tree contains the dormant
helper payload; APT installation performs automatic OBS setup. Use
[the Debian installation instructions](RUNNING.md#linux) to install the combined
package. Verify fresh install, migration from the split packages, upgrade, reinstall,
removal, desktop launch, autostart, and OBS helper loading on clean target systems.
Also check OBS installation after Companion, incompatible OBS, and OBS removal.
Do not run Companion as root.

## Arch/Omarchy x86_64

Build natively on Arch/Omarchy with CMake 3.28+, a C++20 compiler, Ninja, vcpkg,
GTK4/Libadwaita, Avahi, libsecret, PulseAudio, libjpeg-turbo, librsvg, and native OBS
30+ development files with Qt 6. Bootstrap vcpkg using the pinned baseline as above,
then build `cmake --preset omarchy-x64-release` followed by
`cmake --build --preset omarchy-x64-release --parallel 4`.

This preset uses Arch's library layout and dynamically links its libjpeg-turbo.
It installs the OBS helper directly into `usr/lib/obs-plugins` in a separate package.
To create evaluation packages from the completed build, run from the repository root:

```sh
stage="$PWD/out/package/omarchy-custom"
mkdir -p "$stage/payload/companion" "$stage/payload/helper"
DESTDIR="$stage/payload/companion" cmake --install out/build/omarchy-x64-release \
  --prefix /usr --component Unspecified
DESTDIR="$stage/payload/helper" cmake --install out/build/omarchy-x64-release \
  --prefix /usr --component OBSPlugin
payload="$stage/panelive-companion-0.1.0beta.1-payload.tar.zst"
tar -C "$stage/payload" --zstd -cf "$payload" companion helper
cmake -DPANELIVE_ARCH_VERSION=0.1.0beta.1 \
  "-DPANELIVE_PAYLOAD_SHA256=$(sha256sum "$payload" | cut -d ' ' -f 1)" \
  "-DPANELIVE_RECIPE_OUTPUT=$stage/PKGBUILD" -P packaging/arch/configure-recipe.cmake
(cd "$stage" && makepkg --nocheck)
```

Packaging also requires `makepkg`, `tar`, and `zstd`. Install the resulting Companion
package with `pacman -U`; install the helper package when using OBS. Close OBS during
helper updates. The optional Quickshell widget is installed under
`/usr/share/panelive-companion/omarchy-widget/`. These packages require their own
native runtime validation; they do not share the Debian/Ubuntu compatibility claims.

## Build options and package contents

- Presets select the implemented platform providers. vMix cannot be enabled on macOS
  or Linux. XSplit and Wirecast remain off.
- Optional Windows NDI builds require your own SDK via `STUDIO_REMOTE_NDI_SDK_DIR`,
  `STUDIO_REMOTE_ENABLE_VMIX_NDI=ON`, and the appropriate vMix adapter options.
- Remote tally publication is disabled in this source version. The endpoint file
  `config/remote-tally-service.json` contains reserved `.invalid` addresses; changing
  that file alone does not enable the disabled application routes.
- Preserve `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md`, dependency license texts and
  the helper's GPL license when packaging. CMake installs dependency notices from
  the resolved vcpkg tree. See [third-party notices](../THIRD_PARTY_NOTICES.md).
- Evaluate startup, pairing, commands, Program/Preview, audio and clean shutdown on
  the target system before distributing a custom build. A successful compile alone
  does not verify live integration or platform compatibility.
