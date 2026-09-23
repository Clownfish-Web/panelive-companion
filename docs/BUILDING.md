# Build an evaluation copy

Build on the target operating system, from the repository root. These instructions
produce local evaluation packages, not signed/notarized official releases. Use separate
build trees per platform and architecture. The source version is `0.1.0-beta.1`, pinned in `vcpkg.json` and read by both
Companion and OBS helper builds.
Private test harnesses and release automation are intentionally absent from this tree.

## Shared inputs

- Git, CMake **3.28+**, Ninja, and a C++20 compiler. Use a checkout path without
  spaces: the FFmpeg overlay rejects source paths containing spaces.
- vcpkg, using the `builtin-baseline` committed in `vcpkg.json`. Keep the checked-in
  `cmake/overlays` FFmpeg port: its version and Windows DLL names are part of this build.
- OBS Studio and matching OBS development inputs for the separate helper.
- An internet connection for the initial dependency downloads. Cold builds can take
  substantially longer than subsequent builds.

Clone the repository:

```sh
git clone https://github.com/Clownfish-Web/obs-monitor-companion.git panelive-companion
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
ditto -c -k --keepParent "$package" "$package.zip"
shasum -a 256 "$package.zip"
```

The Companion is `panelive-companion.app`; the separate helper is
`obs-plugin/obs-plugins/panelive-companion-obs-plugin.plugin`. CMake signs both
locally with an ad-hoc signature by default. To use your own identity, configure
`STUDIO_REMOTE_MACOS_CODESIGN_IDENTITY` for Companion and `OBS_PLUGIN_CODESIGN_IDENTITY`
for the helper. Local signatures do not establish notarization or release acceptance.
See [running instructions](RUNNING.md#macos).

## Linux x86_64 (Ubuntu)

Ubuntu 22.04 is the intended floor; Ubuntu 24.04 is also a target. Use a native Linux
checkout, CMake 3.28+ (22.04's default CMake is too old), and an OBS/Qt6 development
installation matching the OBS you will run. Distribution OBS packages must supply
both `libobs` and `obs-frontend-api` CMake configurations. Older Qt5-based OBS packages
and sandboxed Flatpak/Snap OBS installations are not covered by these instructions.

```sh
sudo apt-get update
sudo apt-get install --yes \
  build-essential ca-certificates cmake curl git dpkg-dev fakeroot \
  libadwaita-1-dev libavahi-client-dev libdbus-1-dev libglib2.0-dev \
  libgtk-4-dev libsecret-1-dev libjpeg-dev libobs-dev libpulse-dev \
  nasm ninja-build pkg-config qt6-base-dev tar unzip zip python3 \
  autoconf automake libtool
git clone https://github.com/microsoft/vcpkg .buildtools/vcpkg
baseline=$(python3 -c 'import json; print(json.load(open("vcpkg.json"))["builtin-baseline"])')
git -C .buildtools/vcpkg checkout "$baseline"
bash .buildtools/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT="$PWD/.buildtools/vcpkg"
cmake --preset linux-x64-release
cmake --build --preset linux-x64-release --parallel 4
cpack --config out/build/linux-x64-release/CPackConfig.cmake -G DEB
sha256sum out/package/linux/*.deb
```

Linux builds the OBS helper as part of the top-level build. CPack writes a Debian
package under `out/package/linux/`. For an unpacked staging tree instead:

```sh
cmake --install out/build/linux-x64-release \
  --prefix "$PWD/out/package/linux-custom/usr"
```

Use [the Debian installation instructions](RUNNING.md#linux) to install the helper,
desktop integration and system dependencies together. Do not run the Companion as root.

## Build options and package contents

- Presets select the implemented platform providers. vMix cannot be enabled on macOS
  or Linux. XSplit and Wirecast remain off.
- Optional Windows NDI builds require your own SDK via `STUDIO_REMOTE_NDI_SDK_DIR`,
  `STUDIO_REMOTE_ENABLE_VMIX_NDI=ON`, and the appropriate vMix adapter options.
- Remote tally endpoints come from `config/remote-tally-service.json`. Override
  `STUDIO_REMOTE_TALLY_SERVICE_CONFIG` only if using a compatible service
  as described by `cmake/RemoteTallyService.cmake`.
- Preserve `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md`, dependency license texts and
  the helper's GPL license when packaging. CMake installs dependency notices from
  the resolved vcpkg tree. See [third-party notices](../THIRD_PARTY_NOTICES.md).
- Evaluate startup, pairing, commands, Program/Preview, audio and clean shutdown on
  the target system before distributing a custom build. A successful compile alone
  does not verify live integration or platform compatibility.
