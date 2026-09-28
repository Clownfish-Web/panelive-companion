# PaneLive Companion 0.1.0-beta.1 — Linux installers

Both installers include Companion, its GTK4/Libadwaita and media libraries, fonts,
SVG loader, dependency licenses and matching OBS helper. They install the private
application runtime in `/opt/panelive-companion`, register the `panelive-companion`
command and add **PaneLive Companion** to the desktop application menu.

Use the `.deb` on Ubuntu/Debian. Use the CLI installer on other compatible Linux
systems. Install one format at a time. Close Companion and OBS before installing,
upgrading or removing them. Run the installed application as your normal user.

## Requirements

An **x86_64, glibc 2.35+ Linux desktop**, with Ubuntu 22.04 as the build floor.
The host provides a graphical X11 or Wayland session, session D-Bus and an unlocked
Secret Service keyring. Audio uses the desktop's PulseAudio or PipeWire PulseAudio
service; LAN discovery uses Avahi. The application libraries are bundled; desktop
services and optional OBS are supplied by the host. The UI uses GTK's CPU renderer.
Linux 5.15 is the declared kernel floor; a dedicated 5.15 VM has not been validated.
Older glibc, musl/Alpine, ARM and other incompatible systems require a source build.

## Verify downloads

Download an installer and `SHA256SUMS-linux` from the same release, then run:

```sh
sha256sum --check --ignore-missing SHA256SUMS-linux
```

The installer you downloaded must report `OK`. These beta installers are unsigned;
checksums verify the bytes, not publisher identity.

## Ubuntu and Debian — APT installation

```sh
sudo apt install ./panelive-companion-0.1.0-beta.1-linux-x64.deb
panelive-companion
```

The package installs the command in `/usr/bin` and desktop integration under
`/usr/share`. APT manages installation, upgrades and removal. The package version
is `0.1.0~beta.1`, preserving Debian prerelease ordering and upgrade compatibility.
Its application runtime libraries are private, so it does not need system GTK4,
Libadwaita or media-library packages. APT may recommend normal desktop services
such as a keyring or Avahi when those are missing.

Upgrade by installing the newer `.deb` with the same command. To remove:

```sh
sudo apt remove panelive-companion
# Or remove package-managed system state too:
sudo apt purge panelive-companion
```

The installer supports replacing the earlier, system-library-dependent Companion
`.deb`. For an older build with the same version, use:

```sh
sudo apt install --reinstall ./panelive-companion-0.1.0-beta.1-linux-x64.deb
```

If you used the generic CLI installer instead, uninstall that first.

## Other compatible Linux systems — CLI installation

```sh
tar -xzf panelive-companion-0.1.0-beta.1-linux-x64-installer.tar.gz
cd panelive-companion-0.1.0-beta.1-linux-x64-installer
sudo ./install.sh
panelive-companion
```

This performs a system installation. It registers the command in `/usr/local/bin`,
the desktop entry and icon under `/usr/local/share`, and an uninstaller in
`/usr/local/sbin`. The extracted installer directory may be deleted afterward.
Run the newer release's `sudo ./install.sh` to upgrade an existing CLI installation.
The installer preserves unmanaged files and refuses to overwrite an APT installation.

To uninstall:

```sh
sudo /opt/panelive-companion/uninstall.sh
```

Both removal routes preserve per-user settings and credentials. Disable sign-in
startup in Companion before uninstalling. Its startup entry uses the installed
launcher, so ordinary upgrades do not change the startup path.

## OBS integration

The standalone teleprompter works without OBS. For OBS features, install compatible
**native OBS 30+ with Qt 6 and libobs.so.30**. Flatpak/Snap OBS cannot load this helper.
Both installers automatically check OBS and activate the bundled helper when its
ABI, libraries and symbols are compatible. They preserve unrelated helper files.
The helper uses OBS's own libraries inside the OBS process.

If OBS is absent or incompatible, Companion still installs and the helper remains
inactive. The Adapters page reports its status. The `.deb` uses APT triggers to
recheck when OBS changes. After adding or changing OBS with a generic installation,
close OBS and run:

```sh
sudo /opt/panelive-companion/configure-obs-helper
```

Then start OBS again, enable its authenticated WebSocket server, and configure the
OBS adapter in Companion. Remove older manually installed/per-user copies of the
PaneLive helper to avoid duplicate helper installations. Uninstalling Companion
removes only its own managed system helper links.

## Source and licenses

Source: https://github.com/Clownfish-Web/panelive-companion
Licenses and runtime dependency provenance are installed under
`/opt/panelive-companion/usr/share/panelive-companion/`.
The release's `linux-build-manifest.json` records source hashes, build inputs,
bundled distro package versions and artifact hashes. `linux-VALIDATION.md` records
checks performed on those exact installers. Test a full live production session
with your own devices before relying on this beta.
