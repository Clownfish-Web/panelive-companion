# Linux support and validation

PaneLive Companion provides an **amd64 / x86_64 `.deb` installer** for Ubuntu and
Debian, and a **CLI system installer** for other compatible Linux desktops. Both
bundle their application libraries under `/opt/panelive-companion` and are built
in Ubuntu 22.04 userspace. See [installation instructions](../packaging/linux/README.md).

## Supported distributions

| Distribution | Versions | Verification |
| --- | --- | --- |
| Ubuntu | 22.04, 24.04, 26.04 | Package lifecycle and desktop/runtime smoke checks on every version; additional native desktop verification on 24.04 |
| Debian | 12, 13 | Package lifecycle and desktop/runtime smoke checks on both versions |

The original native `.deb` was verified on these targets on **2026-09-24**.
For the new installers with bundled libraries, the release’s `linux-VALIDATION.md`
records the checks and exact installer hashes.
Other Ubuntu/Debian derivatives and newer releases are compatibility candidates when
they supply the required libraries. They are not yet advertised as verified targets.
Linux ARM, RPM packages and other distribution families are outside this release's
tested package scope.

## Runtime requirements

- An x86_64 desktop with current distribution updates. Install the package using
  `sudo apt install ./panelive-companion-0.1.0-beta.1-linux-x64.deb` to resolve dependencies.
- The declared support floor is **Linux 5.15 and Ubuntu 22.04 userspace**, including
  GTK 4.6 and Libadwaita 1.1. A newer kernel alone does not provide compatible desktop
  libraries.
- The bundled-runtime release requires **glibc 2.35+**. GTK4, Libadwaita, the C++
  runtime and application media libraries are bundled privately. The `.deb` declares
  its glibc requirement to APT and recommends normal desktop services.
- A graphical user session, session D-Bus and an unlocked Secret Service keyring.
  Avahi supplies LAN discovery. Run Companion as your normal desktop user.

The standalone teleprompter does not require OBS. The Companion installer has no OBS
dependency.

## OBS helper compatibility

The same installer includes Companion and the matching OBS helper. Setup activates
the helper automatically for native **OBS 30 or newer, Qt 6 and the `libobs.so.30`
ABI**, after checking its required libraries and symbols. Close OBS before installing
or upgrading Companion, then restart it.

With no compatible OBS installed, the helper stays dormant and the teleprompter
remains usable. Installing or updating native OBS through APT automatically rechecks
helper setup. The Adapters page shows setup status. APT replaces the old separate
helper package during migration; uninstallation removes the bundled payload and
managed OBS links. Setup preserves foreign helper files and reports conflicts.

Helper installation and loading were verified with:

| Desktop userspace | Native OBS version |
| --- | --- |
| Ubuntu 22.04 | 30.2.3 |
| Ubuntu 24.04 | 32.2.0 |

Ubuntu 22.04's stock OBS 27 and Debian 12's stock OBS 29 are too old for this helper;
use a compatible newer native OBS package. Flatpak/Snap OBS cannot load the native
helper. The Companion distribution checks do not imply that every distribution's
OBS package has also been verified.

## Earlier native-package verification

The identical Companion installer passed the following checks on all five listed
distribution versions:

- Installation, reinstall, removal and fresh installation through APT.
- Version/help output and rejection of invalid port arguments.
- GTK management window startup under Xvfb and activation of an existing instance.
- HTTPS identity response and rejection of unauthenticated state requests.
- Autostart enable/disable and graceful shutdown.
- Shared-library resolution and installed-file integrity. Slim-image documentation
  exclusions were disabled for the final full-payload integrity check.

Both the Ubuntu 22.04 baseline build and a native Ubuntu 24.04 build passed with
warnings treated as errors. The combined Debian package passed Lintian with no findings.
On the Ubuntu 24.04 host, the previous packages were replaced with the
combined installer; the installed binaries matched their package payloads, runtime
checks passed, and the management window opened on the actual desktop.

The distribution matrix used containers on **Linux 7.0.0-31-generic**. A dedicated
**Linux 5.15 kernel VM has not yet been validated**. The checks establish package and
desktop/runtime compatibility; they do not cover a complete live production session,
mobile pairing, live media streaming or physical audio devices on every distribution.

## Release files

- Combined installer: `panelive-companion-0.1.0-beta.1-linux-x64.deb`.
- Generic CLI installer: `panelive-companion-0.1.0-beta.1-linux-x64-installer.tar.gz`.
- Checksum manifest: `SHA256SUMS-linux`.
- Build provenance and detailed verification: `linux-build-manifest.json` and
  `linux-VALIDATION.md`, when attached to the release.

The Debian package name remains `panelive-companion`, with version
`0.1.0~beta.1`. The filename identifies Linux explicitly on the shared release page.
Use the release's checksum manifest and validation record to identify the exact
tested installer bytes. The release artifacts live under `out/release/v0.1.0-beta.1/linux/`;
superseded local packages are archived outside that upload directory.

Release availability and signing status are stated in
[GitHub Releases](https://github.com/Clownfish-Web/panelive-companion/releases).
See [downloads and verification](DOWNLOADS.md), [installation](RUNNING.md#linux), and
the [Ubuntu 22.04 build instructions](BUILDING.md#linux-x86_64-ubuntu-and-debian).
