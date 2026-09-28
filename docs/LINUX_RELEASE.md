# Linux release installers with bundled dependencies

The `0.1.0-beta.1` release provides a standard x86_64 `.deb` for Ubuntu/Debian and a
`.tar.gz` containing a CLI system installer for other compatible Linux desktops.
Both install the same private application runtime under `/opt/panelive-companion`,
plus the command, desktop-menu integration and matching optional OBS helper.
See [installation and requirements](../packaging/linux/README.md).

## Build

On an x86_64 Linux host with Podman, run from the repository root:

```sh
bash tools/release/build-linux-packages.sh
```

The script builds the pinned Ubuntu 22.04 container recipe, uses the committed
vcpkg baseline in a separate Jammy checkout, and builds Companion and its OBS helper
with warnings treated as errors. It bundles the resolved library dependencies
except host glibc, plus the GTK/GIO modules, SVG loader, fonts and licenses. Every
shipped ELF file is checked against the glibc 2.35 floor. GTK's CPU renderer avoids
mixing bundled application libraries with newer host graphics driver libraries.

Use `CMAKE_BUILD_PARALLEL_LEVEL=4` to control parallelism. Subsequent builds may use
`--skip-image-build`. `PANELIVE_LINUX_BUILD_IMAGE` selects an existing build image;
the script still requires Ubuntu 22.04 inside it. A cold build requires network
access and substantial disk space. Do not concurrently use its vcpkg checkout.

The upload directory is `out/release/v0.1.0-beta.1/linux/`:

- `panelive-companion-0.1.0-beta.1-linux-x64.deb`
- `panelive-companion-0.1.0-beta.1-linux-x64-installer.tar.gz`
- `SHA256SUMS-linux`
- `linux-build-manifest.json`
- `linux-INSTALL.md`

Superseded output directories are preserved under `out/archive/v0.1.0-beta.1/`.
The release script never uploads or publishes files. Attach the installers and
supporting files to the `v0.1.0-beta.1` GitHub prerelease. Packages are unsigned.

The build manifest records source hashes, dirty-tree state, the container image,
dependency versions, bundled libraries and installer hashes. Installation and
runtime validation is separate and should be attached as `linux-VALIDATION.md`,
with its checksum added to the release manifest.

## Verification

Test fresh installation, upgrade/reinstall, removal and reinstallation through
APT and the CLI installer. Check command and desktop registration, installed-file
integrity, preservation of unmanaged files, and refusal to mix installation methods.
Test migration from the earlier unbundled `.deb` and APT helper reconfiguration.

Verify version/help output, GTK startup, HTTPS identity, authentication rejection,
second-instance activation, autostart and shutdown with system GTK4 unavailable.
All application libraries must resolve from `/opt/panelive-companion`; glibc and
desktop services remain host-provided. Verify helper activation/loading against
native OBS, inactive behavior without compatible OBS, and link cleanup on removal.
Containers do not establish the Linux 5.15 kernel floor or live production behavior.
