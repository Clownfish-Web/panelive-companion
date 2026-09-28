# Downloads and SHA-256 verification

Download prebuilt installation packages from
[GitHub Releases](https://github.com/Clownfish-Web/panelive-companion/releases).
Choose the OS and architecture listed on the release. Release notes describe the actual
artifacts, supported backend versions and signing status. If no matching asset is listed,
use the [source build manual](BUILDING.md); availability is not implied by a build preset.
GitHub's automatic source ZIP/tarball downloads are source code, not installers.

Each binary release should include a platform checksum file such as `SHA256SUMS-linux`
(or an adjacent `.sha256` file)
covering the exact downloadable bytes. Download the installer and checksum from the
same release. Do not substitute hashes of an extracted executable for the installer hash.

## Windows PowerShell

For an adjacent checksum file in standard `hash  filename` format:

```powershell
$file = 'panelive-companion-0.1.0-beta.1-windows-x64-setup.exe'
$expected = ((Get-Content -LiteralPath "$file.sha256" -Raw).Trim() -split '\s+')[0]
$actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
if ($expected -notmatch '^[0-9a-fA-F]{64}$' -or $actual -ine $expected) {
  throw 'SHA-256 verification failed. Do not install this file.'
}
'SHA-256 verified'
```

For `SHA256SUMS`, use the hash on the line whose filename exactly matches your download.
`Get-FileHash -Algorithm SHA256 -LiteralPath 'path-to-installer.exe'` displays the actual hash.

## macOS

From the directory containing the release assets and checksum manifest:

```sh
shasum -a 256 -c SHA256SUMS-macos
```

Use the `macos-arm64.pkg` asset on Apple silicon and `macos-x86_64.pkg` on Intel.
The full filenames include the release version. Both require macOS 13 or later and
matching native OBS 32.2.x for the integrated installer. The ZIP is an evaluation
archive with a separate helper installation tool. Read the release notes for signing
and notarization status before installing. See [macOS packaging](MACOS_RELEASE.md).

## Linux

The same **amd64 `.deb` installer** supports the verified Ubuntu 22.04, 24.04 and
26.04, and Debian 12 and 13 desktop targets. Download
`panelive-companion-0.1.0-beta.1-linux-x64.deb` and `SHA256SUMS-linux`.
The installer contains Companion and its matching OBS helper; no second helper
download is needed. See [Linux support and validation](LINUX_SUPPORT.md) for native
OBS requirements and automatic setup behavior.

```sh
sha256sum --check SHA256SUMS-linux
```

These commands report missing files if you downloaded only some of the manifest's
assets. The artifact you intend to install must report `OK`. For a single adjacent
checksum file, pass that filename instead of `SHA256SUMS`.

A matching hash verifies that the bytes match the published checksum. It does not
replace the platform's code signature or independently authenticate the publisher.
After verification, follow [installation and first-run instructions](RUNNING.md).

## Windows release artifacts

The [0.1.0-beta.1 checksum manifest](../releases/0.1.0-beta.1/SHA256SUMS) records the
Windows x64 setup executable and evaluation ZIP built from this public source tree:

- `panelive-companion-0.1.0-beta.1-windows-x64-setup.exe`
- `panelive-companion-0.1.0-beta.1-windows-x64.zip`

Both contain the renamed `panelive-companion-obs-plugin` helper. These evaluation
packages are unsigned. The checksum manifest does not itself publish the binaries;
download availability is determined by GitHub Releases. Checksums accompany the
archives and are not embedded inside the archive whose bytes they describe.

## Initial public release metadata

The release title is **PaneLive Companion 0.1.0-beta.1**, the tag is
`v0.1.0-beta.1`, and GitHub's **Set as a pre-release** flag must be enabled.
The [release metadata](../releases/0.1.0-beta.1/release.json) pins those values and
starts in draft mode so the packages and checksums can be attached before publication.

The display name is **PaneLive Companion**. Package managers and executable paths use
the machine identifier `panelive-companion`; the OBS helper uses
`panelive-companion-obs-plugin` and displays **PaneLive Companion**. The public GitHub
repository is `Clownfish-Web/panelive-companion`.
