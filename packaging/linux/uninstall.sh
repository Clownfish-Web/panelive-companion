#!/bin/sh
set -eu
[ "$#" -eq 0 ] || { echo 'Usage: sudo /opt/panelive-companion/uninstall.sh' >&2; exit 2; }
[ "$(id -u)" -eq 0 ] || { echo 'Run this uninstaller with sudo.' >&2; exit 1; }
app=/opt/panelive-companion
[ ! -L "$app" ] && [ "$(cat "$app/.installation" 2>/dev/null || true)" = generic ] || {
    echo 'This uninstaller only removes a generic CLI installation. For a .deb installation, use sudo apt remove panelive-companion.' >&2
    exit 1
}
"$app/configure-obs-helper" --remove
remove_link()
{
    if [ -L "$1" ] && [ "$(readlink "$1")" = "$2" ]; then rm -- "$1"; fi
}
icon=com.clownfishweb.PaneLiveCompanion
remove_link /usr/local/bin/panelive-companion "$app/launch.sh"
remove_link /usr/local/sbin/panelive-companion-uninstall "$app/uninstall.sh"
remove_link /usr/local/share/man/man1/panelive-companion.1.gz "$app/usr/share/man/man1/panelive-companion.1.gz"
remove_link "/usr/local/share/applications/$icon.desktop" "$app/usr/share/applications/$icon.desktop"
remove_link "/usr/local/share/icons/hicolor/256x256/apps/$icon.png" "$app/usr/share/icons/hicolor/256x256/apps/$icon.png"
rm -rf -- "$app"
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/local/share/applications || true
fi
echo 'PaneLive Companion uninstalled. Per-user settings and credentials were preserved.'
