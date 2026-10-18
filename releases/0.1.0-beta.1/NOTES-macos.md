PaneLive Companion 0.1.0-beta.1 is an evaluation prerelease.

macOS downloads:

- **Apple silicon:** `panelive-companion-0.1.0-beta.1-macos-arm64.pkg`
- **Intel:** `panelive-companion-0.1.0-beta.1-macos-x86_64.pkg`
- Matching `.zip` archives are available for manual evaluation.
- Download `SHA256SUMS-macos` to verify the artifacts.

Requires macOS 13 or later. The integrated installer requires matching native
OBS Studio 32.2.x with Qt 6.11.1. It installs Companion, its matching OBS helper
and the native integration component. PaneLive Teleprompter is included.
Quit OBS and Companion before installing. This October 4 development rebuild moves
the macOS TLS identity to user-only files and uses a new encrypted backend credential
namespace. It does not import old Keychain items: re-enter backend passwords/tokens
and pair clients again. For standalone use without OBS, use the application from the ZIP.

The October 18 rebuild includes the PaneLive website and client store sidebar banners
and the bundled Solo Travel demo for newly created teleprompter libraries. The version
and artifact filenames remain unchanged.

**Signing:** the app, helper and integration component are signed with Developer ID
Application; the PKG installers are signed with Developer ID Installer. Both use
Apple-issued certificates and secure timestamps. The October 18 rebuild replaces
both architectures' PKGs and ZIPs. All four freshly packaged artifacts were accepted
by Apple's notarization service on October 18. Tickets are stapled to both PKG
installers and to the Companion and integration application bundles inside both ZIPs.
Ticket validation and Gatekeeper assessments passed with Notarized Developer ID.
SHA256SUMS-macos contains the finalized artifacts' checksums for GitHub distribution.

The release workflow checks Release compilation, public packaging and Mach-O
tests, signatures, deployment metadata, dependency resolution, extracted installer
payload hashes, installation scope and required choices. The Apple silicon build
runs natively; Intel executable smoke checks run under Rosetta on Apple silicon.
The October 18 clean rebuild passed all 23 public release-tool tests for each architecture,
plus package signature, payload and installer-choice verification. Private
application/UI/IPC suites are not included in the public source workflow.
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
