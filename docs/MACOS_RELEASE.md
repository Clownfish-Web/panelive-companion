# macOS build and release packaging

The public source includes the Companion, OBS helper, native integration component,
installer scripts, artwork, dependency overlays, license staging, and release tools.
Build separate `arm64` and baseline `x86_64` packages with a macOS 13.0 deployment
target. There is no Universal 2 package. See [BUILDING.md](BUILDING.md) for tools and
the pinned vcpkg baseline. No private development checkout is required.

## Configure

Use Xcode with Swift 6, CMake 3.28+, Ninja, Python 3, and the vcpkg prerequisites
listed in the build manual. Install matching native OBS 32.2.2. Obtain its source
and the OBS dependency archives described in that manual. These example commands
use versioned extraction directories:

| Archive | SHA-256 from OBS 32.2.2 dependency metadata |
| --- | --- |
| `macos-deps-2026-07-15-universal.tar.xz` | `4ecb4c598dfa853168df6c2a0c4e0ffec8495a81fbd1ba051ef88ecd5e0f7e53` |
| `macos-deps-qt6-2026-07-15-universal.tar.xz` | `d4b8058612a7067e44b2205fe7925ee24e9ec6b15ac8c29d3e6230c70030b102` |

Extract into `.buildtools/obs-deps-2026-07-15-universal` and
`.buildtools/obs-deps-qt6-2026-07-15-universal`. Clone OBS tag `32.2.2` into
`.buildtools/obs-studio-32.2.2`. Keep downloaded dependencies, credentials, signing
keys, local environment files and generated artifacts out of Git.

Select an Apple-issued code signing certificate available in your login Keychain:

```sh
security find-identity -v -p codesigning
export PANELIVE_SIGNING_IDENTITY='Developer ID Application: Your Name (TEAMID)'
export PANELIVE_COMPANION_MACOS_INSTALLER_IDENTITY='Developer ID Installer: Your Name (TEAMID)'
export VCPKG_ROOT="$PWD/.buildtools/vcpkg"
cmake --preset macos-arm64-release \
  -DSTUDIO_REMOTE_MACOS_CODESIGN_IDENTITY="$PANELIVE_SIGNING_IDENTITY"
cmake -S obs-plugin -B out/build/obs-plugin-macos-arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DOBS_SOURCE_DIR="$PWD/.buildtools/obs-studio-32.2.2" \
  -DOBS_DEPENDENCIES_DIR="$PWD/.buildtools/obs-deps-2026-07-15-universal" \
  -DOBS_QT_DIR="$PWD/.buildtools/obs-deps-qt6-2026-07-15-universal" \
  -DOBS_APP_PATH=/Applications/OBS.app \
  -DOBS_PLUGIN_CODESIGN_IDENTITY="$PANELIVE_SIGNING_IDENTITY"
```

For Intel, use preset `macos-x64-release`, helper build directory
`out/build/obs-plugin-macos-x64`, and `CMAKE_OSX_ARCHITECTURES=x86_64`. Set
`OBS_APP_PATH` to an Intel OBS 32.2.2 bundle. On Apple silicon, extract the official
Intel OBS disk image into `.buildtools/obs-studio-32.2.2-intel/OBS.app`, preserving
your installed ARM OBS. Rosetta must be available for executable smoke checks.
The Intel disk image SHA-256 is
`f8d8afe3dffdc86efa0698c02ff0c997866bac3e6208ddaf56d37108baacf197`.
NASM 3.01 must be on PATH for the Intel media dependencies. Configure architectures
sequentially because vcpkg shares its working directories.

## Compile and package

```sh
bash tools/release/build-macos-package.sh --architecture arm64
bash tools/release/build-macos-installer.sh --architecture arm64
# After configuring the Intel trees:
bash tools/release/build-macos-package.sh --architecture x86_64
bash tools/release/build-macos-installer.sh --architecture x86_64
```

Use `--help` for path overrides. Both tools accept an ignored `tools/release/.env`
file or `--environment-file`; command-line options take precedence over process
environment, then file values, then defaults. Environment variable names are listed
in `tools/release/release-environment.sh`. Values are parsed literally, never sourced.

The package builder compiles both public source trees with Release settings, runs
the public release tests, audits Mach-O architecture/deployment target/dependencies,
checks Apple-issued signatures, and stages the app, helper and signed native
integration bundle. It includes licenses and dependency inventories. The private
application/UI/IPC suites are not part of this public source tree and are not
claimed as tested by this workflow.

Compiler source-prefix mapping is configured in CMake for the app, Swift UI, helper,
and vcpkg dependencies. Diagnostics retain logical source paths without exposing the
builder's home directory. The packaging audit rejects embedded `/Users/<name>/` or
`/home/<name>/` paths in product executables. Changing this policy rebuilds cached
dependencies; normal subsequent builds reuse the corrected dependencies. Rebuilding
or re-signing release artifacts requires new notarization before distribution.
Use Swift's `#fileID` for runtime diagnostics: its explicit `#filePath` literal is
not remapped by Swift's compiler metadata flags and would fail the packaging audit.

The installer builder creates a single integrated PKG, expands it again, compares
payload hashes and scripts, verifies signatures, and checks installer choices and
system installation scope. It does not install anything or change user settings.
The required Teleprompter component is included. OBS setup uses the native integration
component and the current `/Library/Application Support/panelive-companion/installation`
layout. See [installer behavior](../packaging/macos/INSTALL_MACOS.txt).

Outputs, where `<version>` comes from `vcpkg.json`:

- `out/package/panelive-companion-<version>-macos-<arch>.zip` and `.zip.sha256`
- `out/installer/panelive-companion-<version>-macos-<arch>.pkg` and `.pkg.sha256`
- `BUILD_VERIFICATION.txt` inside the package's `share/panelive-companion/` directory

Run `bash tools/release/stage-macos-release.sh` after building both architectures
to collect the PKGs, ZIPs and `SHA256SUMS-macos` into `out/release/<version>/macos/`.
That directory contains the binary assets to attach to GitHub Releases. Upload
release binaries as assets; commit the source tooling and documentation normally.
Use tag `v0.1.0-beta.1`, title **PaneLive Companion 0.1.0-beta.1**, and mark the
release as a prerelease. The release metadata defaults to draft.

## Signing and acceptance

The packaging tools require an Apple-issued signature for the app, OBS helper and
integration bundle. Ad-hoc builds remain available through CMake for local source
evaluation, but are rejected by this packaging workflow because rebuilding them
changes their Keychain identity.

Configure the app and helper with a **Developer ID Application** identity for
external distribution. Set `PANELIVE_COMPANION_MACOS_INSTALLER_IDENTITY` in the
process environment or ignored release `.env` file, or pass
`--signing-identity 'Developer ID Installer: Your Name (TEAMID)'` to the installer
builder. It signs the PKG with a secure timestamp, verifies certificate trust and
checks that the signer is the requested identity before publishing the output.
If no installer identity is configured, it creates an unsigned local evaluation PKG.
Apple Development certificates remain suitable for development builds only.

Signing does not notarize the app, ZIP or PKG. Complete Apple's
[notarization workflow](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
before claiming Gatekeeper-ready distribution, then regenerate checksums for any
artifacts changed by stapling. See [Developer ID distribution](https://developer.apple.com/developer-id/).
Signing keys and notarization credentials are never release assets.

Static macOS 13 compatibility audits and Rosetta smoke checks do not establish
runtime acceptance on macOS 13 or physical Intel hardware. Before claiming that
acceptance, verify clean install, upgrades, Keychain continuity, startup, pairing,
OBS helper loading, media and uninstall on those target systems.
