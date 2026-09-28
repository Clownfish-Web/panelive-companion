#!/bin/sh
# System installation for distributions without Debian package management.
set -eu
[ "$#" -eq 0 ] || { echo 'Usage: sudo ./install.sh' >&2; exit 2; }
[ "$(id -u)" -eq 0 ] || { echo 'Run this installer with sudo.' >&2; exit 1; }
[ "$(uname -m)" = x86_64 ] || { echo 'This package requires x86_64 Linux.' >&2; exit 1; }
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app=/opt/panelive-companion
if command -v dpkg-query >/dev/null 2>&1 &&
   dpkg-query -W -f='${Status}' panelive-companion 2>/dev/null | grep -q '^install ok'; then
    echo 'Companion is managed by APT. Use the .deb package, or remove it with APT first.' >&2
    exit 1
fi
if [ -e "$app" ] || [ -L "$app" ]; then
    if [ -L "$app" ] || [ "$(cat "$app/.installation" 2>/dev/null || true)" != generic ]; then
        echo "An unmanaged or package-managed installation exists at $app." >&2
        exit 1
    fi
fi
icon=com.clownfishweb.PaneLiveCompanion
check_link()
{
    if [ -e "$1" ] || [ -L "$1" ]; then
        if [ ! -L "$1" ] || [ "$(readlink "$1")" != "$2" ]; then
            echo "Refusing to replace unmanaged file: $1" >&2
            exit 1
        fi
    fi
}
check_link /usr/local/bin/panelive-companion "$app/launch.sh"
check_link /usr/local/sbin/panelive-companion-uninstall "$app/uninstall.sh"
check_link /usr/local/share/man/man1/panelive-companion.1.gz "$app/usr/share/man/man1/panelive-companion.1.gz"
check_link "/usr/local/share/applications/$icon.desktop" "$app/usr/share/applications/$icon.desktop"
check_link "/usr/local/share/icons/hicolor/256x256/apps/$icon.png" "$app/usr/share/icons/hicolor/256x256/apps/$icon.png"
# Check the actual bundled executable against the host before changing anything.
"$source_dir/payload/launch.sh" --version
install -d -m 755 /opt
stage=$(mktemp -d /opt/.panelive-companion.XXXXXXXX)
trap 'rm -rf -- "$stage"' EXIT HUP INT TERM
cp -a "$source_dir/payload/." "$stage/"
printf '%s\n' generic > "$stage/.installation"
chown -R 0:0 "$stage"
chmod -R go-w "$stage"
chmod 755 "$stage"
backup=
if [ -d "$app" ]; then
    backup=$(mktemp -d /opt/.panelive-companion-old.XXXXXXXX)
    rmdir "$backup"
    mv -- "$app" "$backup"
fi
if ! mv -- "$stage" "$app"; then
    if [ -n "$backup" ]; then mv -- "$backup" "$app"; fi
    exit 1
fi
install -d -m 755 /usr/local/share/man/man1 /usr/local/bin /usr/local/sbin /usr/local/share/applications /usr/local/share/icons/hicolor/256x256/apps
ln -sfn "$app/launch.sh" /usr/local/bin/panelive-companion
ln -sfn "$app/uninstall.sh" /usr/local/sbin/panelive-companion-uninstall
ln -sfn "$app/usr/share/man/man1/panelive-companion.1.gz" /usr/local/share/man/man1/panelive-companion.1.gz
ln -sfn "$app/usr/share/applications/$icon.desktop" "/usr/local/share/applications/$icon.desktop"
ln -sfn "$app/usr/share/icons/hicolor/256x256/apps/$icon.png" "/usr/local/share/icons/hicolor/256x256/apps/$icon.png"
"$app/configure-obs-helper"
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/local/share/applications || true
fi
if [ -n "$backup" ]; then rm -rf -- "$backup"; fi
echo 'PaneLive Companion installed. Launch it from the application menu or run panelive-companion as your normal user.'
echo 'Uninstall with: sudo /opt/panelive-companion/uninstall.sh'
