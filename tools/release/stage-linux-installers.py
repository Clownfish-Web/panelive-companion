#!/usr/bin/env python3
"""Create standard DEB and generic CLI installers around the same private runtime."""
import pathlib
import shutil
import subprocess
import sys

payload, work, assets = map(pathlib.Path, sys.argv[1:4])
version = sys.argv[4]
name = f'panelive-companion-{version}-linux-x64'
app = pathlib.Path('/opt/panelive-companion')
icon = 'com.clownfishweb.PaneLiveCompanion'
package = work / 'deb'
installed = package / app.relative_to('/')
shutil.copytree(payload, installed)
(installed / '.installation').write_text('deb\n')
# Host checkout umasks must not make system-owned payloads group-writable.
for path in [installed, *installed.rglob('*')]:
    if not path.is_symlink():
        path.chmod(path.stat().st_mode & ~0o022)

def symlink(relative, target):
    path = package / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.symlink_to(target)

symlink('usr/bin/panelive-companion', app / 'launch.sh')
symlink('usr/share/man/man1/panelive-companion.1.gz', app / 'usr/share/man/man1/panelive-companion.1.gz')
symlink(f'usr/share/applications/{icon}.desktop', app / f'usr/share/applications/{icon}.desktop')
symlink(f'usr/share/icons/hicolor/256x256/apps/{icon}.png', app / f'usr/share/icons/hicolor/256x256/apps/{icon}.png')
shutil.copytree(payload / 'usr/share/doc/panelive-companion', package / 'usr/share/doc/panelive-companion')
overrides = package / 'usr/share/lintian/overrides/panelive-companion'
overrides.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2('packaging/linux/lintian-overrides', overrides)
overrides.chmod(0o644)
control = package / 'DEBIAN'
control.mkdir()
(control / 'control').write_text(f'''Package: panelive-companion
Version: {version.replace('-', '~', 1)}
Architecture: amd64
Maintainer: ClownfishWeb <support@clownfishweb.com>
Section: video
Priority: optional
Depends: libc6 (>= 2.35)
Recommends: dbus-user-session, gnome-keyring, avahi-daemon
Suggests: obs-studio (>= 30.0)
Conflicts: panelive-companion-obs-plugin
Replaces: panelive-companion-obs-plugin
Installed-Size: {sum(p.stat().st_size for p in package.rglob('*') if p.is_file() and not p.is_symlink()) // 1024}
Description: Secure mobile production control with bundled application libraries
 Native GTK desktop management, OBS integration and a standalone teleprompter.
 Includes a private desktop and media runtime built on Ubuntu 22.04 and the
 matching OBS helper. Requires the host's normal graphical and desktop services.
''')
scripts = {
    'preinst': '''#!/bin/sh
set -eu
app=/opt/panelive-companion
if [ -e "$app" ] || [ -L "$app" ]; then
    if [ -L "$app" ] || [ "$(cat "$app/.installation" 2>/dev/null || true)" != deb ]; then
        echo 'Remove the generic or unmanaged /opt/panelive-companion installation before using APT.' >&2
        exit 1
    fi
fi
''',
    'postinst': '''#!/bin/sh
set -e
case "$1" in
    configure|triggered|abort-upgrade|abort-remove|abort-deconfigure)
        /opt/panelive-companion/configure-obs-helper ;;
esac
''',
    'prerm': '''#!/bin/sh
set -e
case "$1" in
    remove|deconfigure) /opt/panelive-companion/configure-obs-helper --remove ;;
esac
''',
    # postrm must work after /opt's package-owned payload has already been removed.
    'postrm': '''#!/bin/sh
case "$1" in
    remove|purge) set -- --remove ;;
    *) exit 0 ;;
esac
''' + pathlib.Path('packaging/linux/configure-obs-helper').read_text(),
}
for script, content in scripts.items():
    path = control / script
    path.write_text(content)
    path.chmod(0o755)
(control / 'triggers').write_text('''interest-noawait /usr/bin/obs
interest-noawait /usr/local/bin/obs
interest-noawait /usr/lib/x86_64-linux-gnu
interest-noawait /lib/x86_64-linux-gnu
''')
# dpkg -V needs checksums for the installed regular files (excluding control data).
import hashlib
sums = []
for path in sorted(package.rglob('*')):
    if path.is_file() and not path.is_symlink() and control not in path.parents:
        sums.append(f'{hashlib.md5(path.read_bytes()).hexdigest()}  {path.relative_to(package)}\n')
(control / 'md5sums').write_text(''.join(sums))
subprocess.run(['dpkg-deb', '--root-owner-group', '--build', str(package), str(assets / (name + '.deb'))], check=True)
installer = work / (name + '-installer')
installer.mkdir()
shutil.copytree(payload, installer / 'payload')
shutil.copy2('packaging/linux/install.sh', installer / 'install.sh')
shutil.copy2('packaging/linux/README.md', installer / 'README.md')
subprocess.run(['tar', '--owner=0', '--group=0', '-C', str(work), '-czf', str(assets / (name + '-installer.tar.gz')), installer.name], check=True)
