# PaneLive Companion

**Secure mobile production control and monitoring for OBS Studio, vMix, and Streamlabs Desktop.**

Package and executable name: **`panelive-companion`**.
The OBS plugin also displays **PaneLive Companion**; its binary and package name is
**`panelive-companion-obs-plugin`**.

PaneLive Companion connects OBS Studio, vMix, and Streamlabs Desktop to paired Android
and iOS production controllers through an authenticated HTTPS/WebSocket API. Available
controls and monitoring features depend on the selected backend and platform.

For OBS, a separately built helper supplies headless Program and Studio Preview frames
and monitoring audio over WebRTC, without a projector window or desktop capture. On
Windows, vMix supports control, SRT video, and OMT monitoring audio, with optional NDI
adapters. Streamlabs Desktop provides control and performance information on Windows
and macOS. A built-in teleprompter also works independently of these production apps.

This repository contains the native C++20 Companion, its Windows, macOS and Linux
frontends, and the OBS helper. Mobile applications and hosted services are separate
products and are not included here.

[Downloads and SHA-256 verification](docs/DOWNLOADS.md) |
[Build from source](docs/BUILDING.md) | [Install and run](docs/RUNNING.md)

## Features

- **Production control:** adapters for OBS Studio, vMix, and Streamlabs Desktop,
  with controls advertised according to each backend's capabilities. OBS includes
  scene selection, Program/Preview and Studio Mode, transitions,
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
  not require OBS, vMix, or Streamlabs Desktop. Voice recognition runs on the compatible
  master client.
- **Tally and coordination:** recursive source membership for tally evaluation,
  production countdown event relay, and optional remote tally publication. Remote
  publication depends on a compatible hosted service; the service is not bundled.

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

1. Download an available package from [GitHub Releases](https://github.com/Clownfish-Web/panelive-companion/releases)
   and [verify its SHA-256](docs/DOWNLOADS.md), or [build locally](docs/BUILDING.md).
2. Start PaneLive Companion and open **Adapters** in management to configure OBS Studio,
   vMix, or Streamlabs Desktop. Follow the [backend setup instructions](docs/RUNNING.md)
   for your chosen app, or use the standalone teleprompter without a production app.
3. For OBS, install the matching helper while OBS is closed, then enable
   **Tools > WebSocket Server Settings > Enable WebSocket server** with authentication.
   Run Companion in the same user session as OBS and configure its address, port
   (normally `4455`) and password in the OBS adapter.
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

Thanks to the OBS Project, the teams behind vMix and Streamlabs Desktop, and the
maintainers of the libraries listed in the notices. PaneLive Companion is an independent
project, not affiliated with or endorsed by the OBS Project or the makers of vMix or
Streamlabs Desktop.
Product names and trademarks belong to their respective owners.
