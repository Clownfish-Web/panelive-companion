# PaneLive Companion

**Local production control and monitoring for paired mobile clients.**

Package and executable name: **`panelive-companion`**.
The OBS plugin also displays **PaneLive Companion**; its binary and package name is
**`panelive-companion-obs-plugin`**.

PaneLive Companion connects OBS Studio to Android and iOS production controllers through
an authenticated HTTPS/WebSocket API. A separately built OBS helper supplies headless
Program and Studio Preview frames and monitoring audio, which the Companion streams over
WebRTC. There is no need to open a projector window or capture the desktop.

This repository contains the native C++20 Companion, its Windows, macOS and Linux
frontends, and the OBS helper. Mobile applications and hosted services are separate
products and are not included here.

[Downloads and SHA-256 verification](docs/DOWNLOADS.md) |
[Build from source](docs/BUILDING.md) | [Install and run](docs/RUNNING.md)

## Features

- **OBS control:** scene selection, Program/Preview and Studio Mode, transitions,
  source visibility, audio mute and volume, recording and streaming controls,
  profiles and scene collections, thumbnails, and supported hotkeys.
- **Live monitoring:** VP8 Program/Preview video, Opus monitoring audio, audio meters
  and production performance telemetry. Available controls are advertised per backend
  and per client role.
- **Secure pairing:** temporary QR pairing, a pinned gateway certificate, revocable
  device credentials, viewer/operator/admin roles, and local approval for privileged
  operations. Credentials use native operating-system storage.
- **LAN discovery and live state:** DNS-SD discovery, revisioned state updates,
  independent backend reconnects, and queued critical output actions.
- **Native management:** connected clients, adapter controls, settings, pairing,
  diagnostics, and graceful shutdown through the desktop frontend.
- **Teleprompter:** a script library, per-client script delivery, shared sessions and
  presentation settings, controller/master roles, timed pauses and cue markers,
  progress and voice-follow state synchronization, and a standalone adapter that does
  not require OBS. Voice recognition runs on the compatible master client.
- **Tally and coordination:** recursive source membership for tally evaluation,
  production countdown event relay, and optional remote tally publication. Remote
  publication depends on a compatible hosted service; the service is not bundled.
- **Additional production backends:** Windows vMix control with SRT video and OMT
  monitoring audio by default; optional NDI adapters. Streamlabs Desktop has a
  capability-limited control adapter on Windows and macOS.

## Platform and backend scope

The initial public version is **0.1.0-beta.1**, marked as a **prerelease**.
The `0.x` series is for initial development, and the `beta.1` suffix identifies the
first beta under [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
The version is pinned in `vcpkg.json`; GitHub release metadata uses tag `v0.1.0-beta.1`
and `prerelease: true`. Source availability does not imply that an
installer has been published or validated for every platform.

| Platform | Native frontend | Source build scope |
| --- | --- | --- |
| Windows x64 | WinUI 3 / notification area | OBS, vMix, Streamlabs, teleprompter |
| macOS 13+ arm64 / x86_64 | SwiftUI / AppKit | OBS, Streamlabs, teleprompter; evaluation builds |
| Linux x86_64 (Ubuntu) | GTK4 / libadwaita | OBS and teleprompter; Debian packaging |

The OBS helper must match the architecture and OBS/Qt development inputs described
in the [build manual](docs/BUILDING.md). Streamlabs currently provides control and
performance information, without Program/Preview video, thumbnails, audio meters or
monitoring audio. vMix is Windows-only. XSplit and Wirecast directories are scaffolding,
not supported integrations. Windows ARM64 and macOS Universal 2 are not advertised as
validated targets.

## Get started

1. Download an available package from [GitHub Releases](https://github.com/Clownfish-Web/obs-monitor-companion/releases)
   and [verify its SHA-256](docs/DOWNLOADS.md), or [build locally](docs/BUILDING.md).
2. For OBS, install the matching helper while OBS is closed, then start OBS and enable
   **Tools > WebSocket Server Settings > Enable WebSocket server** with authentication.
3. Start PaneLive Companion in the same user session as OBS. Open management and
   configure the OBS address, port (normally `4455`) and password.
4. Select **Pair new client** and scan the temporary QR with a compatible mobile client
   on the same LAN. The gateway's default HTTPS port is `7443`.

See [Install and run](docs/RUNNING.md) for platform commands, backend setup,
firewall considerations and troubleshooting. A compatible paired client is required
for mobile controls and media; the Companion is not a browser dashboard.

## Reading the source

| Path | Responsibility |
| --- | --- |
| `app/` | Application composition and API delivery |
| `core/`, `include/studio_remote/` | Portable policy, state, interfaces and IPC contracts |
| `providers/` | Production backend adapters and teleprompter |
| `platform/`, `ui/` | Native services, capture, credentials and management frontends |
| `protocol/`, `generated/protocol/`, `schemas/` | Protocol code, generated types and JSON schemas |
| `obs-plugin/` | Separately built OBS capture/control helper |
| `cmake/`, `packaging/`, `tools/` | Build integration, installer resources and required dependency helpers |

The Companion does not link to OBS. Its helper runs inside OBS and communicates through
versioned local IPC. This public source tree includes runtime and build inputs plus
evaluation manuals; private work notes, test harnesses, IDE settings and build caches
are omitted.

## Licenses and acknowledgments

The Companion is licensed under **Apache-2.0**; the OBS helper under
**GPL-2.0-or-later**. Third-party components retain their own licenses.

- [Companion license](LICENSE)
- [OBS helper license](obs-plugin/LICENSE)
- [Project attribution](NOTICE)
- [Third-party notices and dependency provenance](THIRD_PARTY_NOTICES.md)

Thanks to the OBS Project and the maintainers of the libraries listed in the notices.
PaneLive Companion is an independent project, not an official or endorsed OBS product.
Product names and trademarks belong to their respective owners.
