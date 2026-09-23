# Third-party notices

PaneLive Companion is Apache-2.0 software, but its build and Windows binary distribution use the independently licensed components below. This file is a routing index, not a substitute for their license texts.

The Windows package installs the corresponding upstream or vcpkg `copyright` files under
`share/panelive-companion/licenses/<component>/copyright`. vcpkg source provenance is
pinned by the `vcpkg.json` baseline; the Windows App SDK artifacts are pinned by URL and SHA-256 in
the CMake integration. The build inputs are described in [the build manual](docs/BUILDING.md).

## Shipped runtime or compiled-in components

| Component | Resolved version | License | Form in the Windows package | License pointer |
| --- | --- | --- | --- | --- |
| CivetWeb | 1.16, vcpkg port revision 2 | MIT | `civetweb.dll`, `civetweb-cpp.dll` | `licenses/civetweb/copyright` |
| FFmpeg | 9.0.1, repository overlay port revision 0 | LGPL-2.1-or-later | Codec, format, and scaling libraries used by the vMix SRT receiver | `licenses/ffmpeg/copyright` |
| libdatachannel | 0.24.5 | MPL-2.0 | `datachannel.dll` | `licenses/libdatachannel/copyright` |
| libjuice | 1.7.2 | MPL-2.0 | `juice.dll`; ICE transport used by libdatachannel | `licenses/libjuice/copyright` |
| libvpx | 1.16.0, vcpkg port revision 2 | BSD-3-Clause | Statically linked VP8 encoder | `licenses/libvpx/copyright` |
| Opus | 1.5.2, vcpkg port revision 1 | BSD-3-Clause | `opus.dll`; monitor-audio encoder | `licenses/opus/copyright` |
| Open Media Transport libomt | 1.0.0.16 | MIT | `libomt.dll`; vMix Headphones audio-only receiver and discovery | `licenses/omt/copyright` |
| libsrtp | 2.8.0 | BSD-3-Clause | SRTP media transport compiled into `datachannel.dll` | `licenses/libsrtp/copyright` |
| libsrt | 1.5.6 | MPL-2.0 | `srt.dll`; vMix Program and Preview SRT receiver transport | `licenses/libsrt/copyright` |
| nlohmann/json | 3.12.0, vcpkg port revision 2 | MIT | Header-only, compiled into gateway binaries | `licenses/nlohmann-json/copyright` |
| OpenSSL | 3.6.3 | Apache-2.0 | `libcrypto-3-x64.dll`, `libssl-3-x64.dll` | `licenses/openssl/copyright` |
| SQLite | 3.53.4 | Public Domain | `sqlite3.dll` | `licenses/sqlite3/copyright` |
| libqrencode | 4.1.1, vcpkg port revision 3 | LGPL-2.1-or-later | `qrencode.dll`, dynamically replaceable | `licenses/libqrencode/copyright` |
| usrsctp | 0.9.5.0, vcpkg port revision 4 | BSD-3-Clause | Compiled into the WebRTC transport dependency | `licenses/usrsctp/copyright` |
| plog | 1.1.11 | MIT | Header-only logging dependency of the WebRTC stack | `licenses/plog/copyright` |
| zlib | 1.3.2, vcpkg port revision 1 | Zlib | `z.dll` | `licenses/zlib/copyright` |
| Windows App SDK / WinUI 3 | 2.4.0 | Microsoft Software License Terms | Shared Windows App Runtime installed by setup; application-local bootstrap DLL | `licenses/windows-app-sdk/copyright` |

No local modifications are made to the source files of libdatachannel or libjuice by this repository. Recipients may obtain the exact upstream source revisions through the pinned vcpkg baseline and package metadata. MPL-2.0 source-availability obligations continue to apply to the MPL-covered files.

libqrencode is shipped as a separate DLL so recipients can replace/relink that LGPL-covered library. The library's complete LGPL terms accompany the binary package at the pointer above.

SQLite's upstream dedication states that SQLite is in the public domain.

## Optional user-installed integration

The build-selected vMix NDI audio adapter and optional Program/Preview NDI video adapter compile
against NDI SDK receiver headers and dynamically load a separately installed NDI runtime. This
repository and its standard Windows package do not
redistribute the NDI SDK or runtime. Enabling or distributing that optional integration requires
the distributor to review and comply with the current NDI SDK license and trademark terms.

The production vMix monitoring-audio path uses the official Open Media Transport `libomt` binary.
It is redistributed under the MIT license from the pinned upstream v1.0.0.16 binary release. The
Companion requests audio only and does not ship or initialize the optional VMX video codec.

## Codec and trademark note

The package implements VP8 through libvpx and monitor audio through Opus. It does not enable H.264. Software copyright licenses do not grant every right that might be relevant to codecs in every jurisdiction; distributors remain responsible for their own patent review.

PaneLive Companion is independent software and is not endorsed by or affiliated with the OBS Project. Names and trademarks belong to their respective owners.

## License texts in this source snapshot

The [licenses](licenses/) directory contains the complete dependency notices from the
pinned Windows dependency set. A custom build installs the notices from its own resolved
dependencies; retain those when redistributing that build.

The separately built [OBS helper](obs-plugin/LICENSE) is GPL-2.0-or-later and includes
the complete GPL version 2 text. It links to the user's OBS and Qt installation, not
the Apache-licensed Companion. OBS and Qt are not bundled in this source repository.
Native macOS frameworks and Linux GTK/libadwaita, Avahi, Secret Service, PulseAudio,
libjpeg and Qt dependencies are supplied by the operating system or package manager
and retain their upstream licenses.

The version table above records the pinned Windows dependency set, not a claim that
every component is shipped on every platform. Optional adapters change the actual
package contents.
