# Install and run

Use the package for your OS and CPU architecture, and verify its hash before installing.
For source builds, first follow [BUILDING.md](BUILDING.md). Choose OBS Studio, vMix
(Windows), or Streamlabs Desktop (Windows/macOS) in the Companion's adapter settings,
or use the standalone teleprompter without a production app. Features vary by backend.
For OBS monitoring, run Companion in the same desktop user session as OBS; the helper
uses local, owner-restricted IPC. Remote OBS WebSocket control does not make the
helper's local video/audio IPC remote.

## Windows

Run the setup executable normally, without **Run as administrator**. It installs
under `%LOCALAPPDATA%\Programs\panelive-companion` and records the installation
for your Windows account. The Windows App Runtime is registered for that account.
Setup requests administrator approval only for a needed firewall change or an OBS
plugin change that Windows denies in the selected OBS folder. Writable OBS folders
need no elevation. User settings, pairing data, and sign-in startup stay in your
account, including when another administrator approves the integration component.
Installation and removal use compiled Inno Setup code; neither launches PowerShell
or a command window. Required OBS and firewall changes share one approval.
Setup and uninstall stop Companion before replacing or deleting files. They first
request a clean shutdown, then force-stop the process after five seconds if needed.
Only the Companion executable in the selected installation directory is targeted.

The default firewall task allows inbound traffic only to the Companion executable,
from the local subnet, on **Private** networks. TCP control and UDP/WebRTC media are
covered, including negotiated media ports. Each installation owns its own rule;
reinstalling reuses a matching rule, and uninstall removes only that rule. Keep a
trusted home/studio Wi-Fi network set to Private. Guest/client isolation and policies
that block local firewall rules require changes by your network administrator.
Uncheck the firewall task if an administrator manages your network access separately.

If an older all-users installation exists, uninstall it first while preserving your
profile. The per-user installer does not remove machine-wide installations or their
firewall rules. If required integration approval is cancelled, setup reports an
incomplete installation; rerun it to finish. Uninstall stops before deleting the
application or profile if an integration cannot be removed.

For a published setup executable, follow its installer prompts. For a custom package,
open PowerShell and set `$package` to the directory produced by the build manual:

```powershell
$package = "$PWD/out/package/panelive-companion-0.1.0-beta.1-windows-x64-custom"
& "$package/redist/WindowsAppRuntimeInstall-x64.exe"
```

Complete the Windows App Runtime installation if it is not already present. The
build instructions compile `installer-tools/panelive-companion-integrations.exe`,
which contains the same installation code used by setup and uninstall. Close OBS,
then install its plugin and enable local-network access together:

```powershell
$integrationArgs = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /APPROOT="{0}" /OBSINSTALL="C:\Program Files\obs-studio" /FIREWALL=add' -f $package
$result = Start-Process "$package/installer-tools/panelive-companion-integrations.exe" `
  -ArgumentList $integrationArgs -WindowStyle Hidden -Wait -PassThru
if ($result.ExitCode -ne 0) { throw 'Companion integration setup did not complete.' }
```

Omit `/OBSINSTALL` for Teleprompter alone. This is a native executable; it requests
one Windows approval only if protected OBS files or the firewall need changing.
Before deleting a custom package, remove the integration using:

```powershell
$integrationArgs = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /APPROOT="{0}" /OBSREMOVE="C:\Program Files\obs-studio" /FIREWALL=remove' -f $package
$result = Start-Process "$package/installer-tools/panelive-companion-integrations.exe" `
  -ArgumentList $integrationArgs -WindowStyle Hidden -Wait -PassThru
if ($result.ExitCode -ne 0) { throw 'Companion integration removal did not complete.' }
```

Uninstall preserves OBS's own settings and plugin-manager preferences.

Restart OBS. Launch the Companion:

```powershell
& "$package/bin/panelive-companion.exe" --open-management
```

Use the notification-area icon to reopen management, pair a client or shut down.
Closing management hides the window. The package's `RUN_CURRENT_MACHINE.cmd` is an
alternative console launcher; it explicitly selects gateway port **8443**. A direct
launch defaults to **7443**. Only one Windows Companion process may run at a time.

## macOS

For a published PKG, follow its installer prompts. Setup places the application in
`/Applications` and updates the shared OBS helper with one administrator approval.
Settings, credentials and paired clients remain per user. For a local build, stage
the integration bundle described in [BUILDING.md](BUILDING.md#macos-arm64-or-x86_64),
then close OBS and install the helper as your normal user:

```sh
package="$PWD/out/package/panelive-companion-0.1.0-beta.1-macos-arm64-custom"
bash "$package/Install-ObsPlugin.sh" --obs-directory /Applications/OBS.app
open "$package/panelive-companion.app" --args --open-management
```

For Intel, use your x86_64 package directory. The helper installs to
`/Library/Application Support/obs-studio/plugins/`, with a link under your
`~/Library/Application Support/obs-studio/plugins/` directory. The native integration
component requests administrator approval when shared files need changing. Remove
a conflicting old personal helper before installation. Restart OBS afterward.
See the packaged [helper installation manual](../packaging/macos/INSTALL_OBS_PLUGIN_MACOS.md)
for details. Keychain and local-network prompts should be handled in your logged-in
session. A locally built, ad-hoc-signed app is an evaluation artifact; published release
notes must state their signing/notarization status.

## Linux

Use the same amd64 `.deb` installer on **Ubuntu 22.04, 24.04 or 26.04**, or
**Debian 12 or 13**, with current distribution updates. See
[Linux support and validation](LINUX_SUPPORT.md) for requirements and test scope.

Install the Companion package, replacing the filename with your artifact:

```sh
sudo apt install ./panelive-companion-0.1.0-beta.1-linux-x64.deb
panelive-companion --open-management
```

The installer **includes the matching OBS helper**. Close OBS before installing or
upgrading Companion, then restart OBS. Setup automatically activates the helper for
native OBS 30 or newer with Qt 6 and the `libobs.so.30` ABI. Stock OBS on Ubuntu 22.04
and Debian 12 is too old; Flatpak/Snap OBS cannot load this native helper.

If OBS is absent or incompatible, installation still completes for teleprompter use.
The **Adapters** page explains helper setup status. Installing, upgrading or repairing
native OBS through APT automatically rechecks setup; removing OBS deactivates the
helper. Companion updates replace the bundled helper with its matching version.
APT removes the obsolete separate `panelive-companion-obs-plugin` package during
migration to this installer. Manually installed helpers are preserved; remove a
conflicting older copy and reinstall Companion if setup reports a conflict.

The release installs its bundled runtime under `/opt/panelive-companion`. The helper
payload is in `usr/lib/panelive-companion/obs-plugin/` beneath that directory. Setup exposes
it to compatible OBS through links under `/usr/lib/x86_64-linux-gnu/obs-plugins/` and
`/usr/share/obs/obs-plugins/`. Uninstall removes the bundled helper and its managed
links while preserving user profiles. Use a graphical user session
with session D-Bus, an unlocked Secret Service keyring and Avahi available for discovery.
The Companion uses the user's XDG state directory. Run it as your normal user,
not through `sudo`.

For other compatible x86_64 Linux desktops, extract
`panelive-companion-0.1.0-beta.1-linux-x64-installer.tar.gz` and run `sudo ./install.sh`.
This installs the same bundled runtime and registers the command and application
menu entry. See [CLI installation, upgrades and removal](../packaging/linux/README.md).

Linux management preserves pending form edits and device selections during status
refreshes. The pairing QR can be enlarged temporarily; close it with the close button
or Escape. If the configured listener port is busy, Companion stays open in network
recovery mode so you can correct its network settings.

## Configure OBS and pair

1. In OBS, enable **Tools > WebSocket Server Settings > Enable WebSocket server** and
   authentication. Note the port and password; the usual port is `4455`.
2. In Companion management, open **Adapters** and enter the OBS address (`127.0.0.1`
   for local OBS), port and password. Confirm the adapter connects.
3. For Studio Preview monitoring, enable OBS Studio Mode. Confirm the matching helper
   loaded in OBS and that Program/Preview are available in Companion status.
4. Put the mobile client on the same reachable LAN. Select **Pair new client**, choose
   the appropriate backend and role, and scan the QR with the compatible client.
5. Check scene selection, source visibility, meters and media. Test recording/streaming
   controls with an evaluation setup before using them for a live production.

Pairing codes expire after five minutes and are single-use. Pairing establishes the
Companion certificate fingerprint; device credentials can be revoked in management.
Do not share a live pairing QR. Enter backend credentials in the local management UI.

The gateway defaults to HTTPS/WSS port `7443`. Allow the Companion through your host
firewall on your private LAN; discovery uses mDNS and WebRTC media also requires UDP
connectivity. Guest Wi-Fi isolation, VPN routing and host firewalls can prevent pairing
or media. This is a LAN gateway: no router port forwarding is needed for local use.

A direct launch can select another gateway port or an isolated evaluation profile:

```text
panelive-companion --gateway-port 8443 --data-directory PATH --open-management
```

On macOS, use the executable inside `Contents/MacOS/` or `open ... --args`. Saved
endpoint overrides belong to the selected profile. Keep OBS and Companion under the
same user identity for helper access.

## Other adapters

- **vMix (Windows):** enable vMix's HTTP control service and configure its host, port
  (normally `8088`) and credentials on the Adapters page. The standard build selects
  SRT for Program/Preview and OMT for monitoring audio. Configure the corresponding
  vMix outputs and audio routing; HTTP connectivity alone does not enable media.
- **Streamlabs Desktop (Windows/macOS):** configure the local Streamlabs adapter and,
  where requested, its remote-control endpoint/token. The transport and available
  features differ from OBS. No OBS helper is installed into Streamlabs. This adapter
  provides scene/source/audio/output control and performance, not media monitoring.
- **Teleprompter:** select the built-in standalone adapter for use without a production
  backend. Manage scripts and client assignments in the Companion, then use a compatible
  paired teleprompter client. Shared sessions require assigned controller/master roles.
- **Remote tally:** publication requires local permission and a reachable compatible
  service. The default service endpoints are public configuration in
  `config/remote-tally-service.json`. Ordinary LAN control does not require remote tally.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| OBS control does not connect | OBS WebSocket enabled, correct password/host/port, adapter running |
| Control works but video/audio is absent | Matching helper loaded, same user/session, Studio Mode for Preview, active sources and audio routing |
| Client cannot find Companion | Same LAN, multicast discovery allowed, no client isolation; use client-supported QR pairing |
| Gateway fails to bind | Port conflict or Windows reserved port; select another port such as `8443` |
| Windows app fails before showing management | Matching Windows App Runtime installed; run from the complete package with its DLLs |
| macOS helper does not load | OBS, helper and Qt version/architecture match; inspect OBS's log |
| Linux credentials are unavailable | Desktop Secret Service/keyring running and unlocked |

Inspect the management diagnostics and OBS log without publishing credentials, device
tokens or pairing material. Quit through the native management/tray action before
replacing binaries. Closing a management window is not a reliable way to stop the gateway.

## Package identity

The software is named **PaneLive Companion**. Packages, executable names, install/data
directories and service identity use `panelive-companion`. The OBS helper binary is
named `panelive-companion-obs-plugin` and is bundled in the Linux installer.
Existing profiles from the earlier
Companion package identity are not automatically migrated; configure and pair the new
installation before evaluation.

The OBS module also displays **PaneLive Companion**. The installer keeps Companion
and its helper on the same release because their shared-memory names are coordinated.
Remove manually installed older helper copies to avoid loading two helpers in OBS.
