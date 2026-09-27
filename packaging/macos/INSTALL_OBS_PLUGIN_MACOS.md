# Native macOS development package

This package contains the Companion application and a separately licensed OBS helper. It is a
native architecture development build, not a notarized release. Use the package matching the Mac
and OBS architecture (arm64 or x86_64), on macOS 13 or newer. The current helper requires OBS
32.2.x and its Qt 6.11.1 runtime. The integration component must retain its configured build signing
identity. The helper uses the matching
OBS application's frameworks; see the packaged macOS build instructions for the tested build inputs.

The integrated `.pkg` installer performs the helper installation automatically. These standalone
instructions apply to development ZIPs or an explicit helper reinstall from
`/Library/Application Support/panelive-companion/installation`.

## Install the OBS helper

1. Close OBS before installing or replacing its helper.
2. Open `Install-ObsPlugin.command` from this package, or run the shell installer below.
3. Start OBS again, then open `panelive-companion.app`.
4. Configure the OBS WebSocket connection on the Companion's Adapters page. Use its Status page to
   inspect the OBS connection and helper feeds.

For OBS installed at another location:

```sh
./Install-ObsPlugin.sh --obs-directory "/path/to/OBS.app"
```

Run the installer as the user who runs OBS, without `sudo`. Its separate signed integration
component requests administrator approval only when shared files need changing. It installs:

```text
/Library/Application Support/obs-studio/plugins/panelive-companion-obs-plugin.plugin
```

A per-user symlink at `~/Library/Application Support/obs-studio/plugins/panelive-companion-obs-plugin.plugin`
lets OBS discover the shared bundle. Remove a previous regular user helper or conflicting link
explicitly before installation. An unchanged shared helper requires no elevation.

The installer validates the packaged helper binary, GPL license, locale, bundle identity and
signature before copying. It replaces the complete bundle and restores the previous version if
replacement fails. It refuses while OBS is running and never closes or restarts OBS itself.
It leaves the signed `OBS.app`, OBS profiles, scene collections, hotkeys and Companion settings alone.
The standalone installer does not register a Companion installation record, matching its Windows
counterpart. The integrated PKG writes the per-user installation record after application placement and before
automatic helper setup.

The Apache-2.0 Companion remains separate from the GPL-2.0-or-later helper under `obs-plugin`.
Installing the helper does not install the Companion into `/Applications` or enable login startup.
Keep the source package available if you need to reinstall the same helper version.

## Verify an installation

```sh
codesign --verify --strict \
  "$HOME/Library/Application Support/obs-studio/plugins/panelive-companion-obs-plugin.plugin"
shasum -a 256 \
  "obs-plugin/obs-plugins/panelive-companion-obs-plugin.plugin/Contents/MacOS/panelive-companion-obs-plugin" \
  "$HOME/Library/Application Support/obs-studio/plugins/panelive-companion-obs-plugin.plugin/Contents/MacOS/panelive-companion-obs-plugin"
```

Both executable hashes must match. The installed bundle contains its license at
`Contents/Resources/LICENSE` and its locale at `Contents/Resources/locale/en-US.ini`.
Actual OBS module startup, headless media, mobile acceptance and final release signing remain
acceptance gates; isolated installer and loader tests do not establish those results.
