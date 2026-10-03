PaneLive Companion 0.1.0-beta.1 is an evaluation prerelease.

macOS downloads:

- **Apple silicon:** `panelive-companion-0.1.0-beta.1-macos-arm64.pkg`
- **Intel:** `panelive-companion-0.1.0-beta.1-macos-x86_64.pkg`
- Matching `.zip` archives are available for manual evaluation.
- Download `SHA256SUMS-macos` to verify the artifacts.

Requires macOS 13 or later. The integrated installer requires matching native
OBS Studio 32.2.x with Qt 6.11.1. It installs Companion, its matching OBS helper
and the native integration component. PaneLive Teleprompter is included.
Quit OBS and Companion before installing. Existing settings, paired clients and
Keychain credentials are preserved by the installer. For standalone use without
OBS, use the application from the ZIP.

**Signing:** the app, helper and integration component are signed with Developer ID
Application; the PKG installers are signed with Developer ID Installer. Both use
Apple-issued certificates and secure timestamps. The ZIP contents retain their
notarization and stapled tickets. The October 3 installer repair rebuilt both
signed PKGs; these replacement PKGs require fresh notarization and stapling before
distribution. Do not reuse the earlier packages' notarization result.

The release workflow checks Release compilation, public packaging and Mach-O
tests, signatures, deployment metadata, dependency resolution, extracted installer
payload hashes, installation scope and required choices. The Apple silicon build
runs natively; Intel executable smoke checks run under Rosetta on Apple silicon.
Private application/UI/IPC suites are not included in the public source workflow.
The October 3 Apple silicon repair passed an actual native Installer upgrade on
the development Mac, including both installer callbacks, shared OBS helper placement,
and Companion launch. Paired iPad clients reconnected and shared teleprompter video
worked. Both expanded PKGs verify executable preinstall/postinstall entry points;
23 release-tool tests passed. Clean installation, physical Intel hardware and older
macOS runtime acceptance remain unverified.

To verify all downloaded macOS assets from their directory:

```sh
shasum -a 256 -c SHA256SUMS-macos
```

If only some assets were downloaded, the other entries report missing files; the
asset being installed must report `OK`.
