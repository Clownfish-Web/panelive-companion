#!/usr/bin/env python3
"""Bundle the Jammy desktop runtime; leave only glibc and desktop services to the host."""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys

root = pathlib.Path(sys.argv[1]).resolve()
lib = root / 'usr/lib'
lib.mkdir(parents=True, exist_ok=True)
# These are one coherent host glibc installation, not application dependencies.
host_libraries = re.compile(r'^(ld-linux-x86-64\.so\.2|lib(c|m|dl|pthread|rt|resolv|util|anl)\.so\.[0-9]+)$')
records = {}
packages = set()

def run(*args):
    return subprocess.check_output(args, text=True).strip()

def record(source, destination):
    source = pathlib.Path(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    records[str(destination.relative_to(root))] = {
        'source': str(source),
        'sha256': hashlib.sha256(destination.read_bytes()).hexdigest(),
    }
    # Ubuntu's merged /usr can differ from dpkg's recorded paths.
    candidates = [str(source), str(source.resolve())]
    if str(source).startswith('/lib/'):
        candidates.append('/usr' + str(source))
    for candidate in candidates:
        result = subprocess.run(['dpkg-query', '-S', candidate], text=True, capture_output=True)
        if result.returncode == 0:
            packages.add(result.stdout.split(': ', 1)[0])
            return
    raise RuntimeError(f'No package provenance for {source}')

def dependencies(binary):
    output = run('ldd', str(binary))
    if 'not found' in output:
        raise RuntimeError(output)
    for name, source in re.findall(r'^\s*(\S+) => (/\S+) ', output, re.M):
        if not host_libraries.fullmatch(name) and name not in seen:
            seen.add(name)
            record(source, lib / name)
            dependencies(source)

seen = set()
dependencies(root / 'usr/bin/panelive-companion')
# dlopen inputs cannot be discovered by inspecting DT_NEEDED.
for source in pathlib.Path('/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders').glob('*.so'):
    record(source, lib / 'gdk-pixbuf-2.0/loaders' / source.name)
    dependencies(source)
assert (lib / 'gdk-pixbuf-2.0/loaders/libpixbufloader-svg.so').exists(), 'SVG loader missing'
query = pathlib.Path('/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/gdk-pixbuf-query-loaders')
record(query, root / 'usr/bin/gdk-pixbuf-query-loaders')
dependencies(query)
# Bundle only the needed GIO modules, never discover host modules with a newer GLib ABI.
for name in ('libdconfsettings.so', 'libgiognutls.so'):
    source = pathlib.Path('/usr/lib/x86_64-linux-gnu/gio/modules') / name
    record(source, lib / 'gio/modules' / name)
    dependencies(source)
subprocess.run(['gio-querymodules', str(lib / 'gio/modules')], check=True)
for source, destination in (
    ('/usr/share/glib-2.0/schemas', 'usr/share/glib-2.0/schemas'),
    ('/usr/share/icons/Adwaita', 'usr/share/icons/Adwaita'),
    ('/usr/share/fonts/truetype/dejavu', 'usr/share/fonts/truetype/dejavu'),
):
    shutil.copytree(source, root / destination, dirs_exist_ok=True)
packages.update(('adwaita-icon-theme', 'fonts-dejavu-core', 'gsettings-desktop-schemas', 'libgtk-4-common'))
# Fontconfig resolves this bundled font path relative to this configuration file.
fonts = root / 'usr/etc/fonts'
fonts.mkdir(parents=True, exist_ok=True)
(fonts / 'fonts.conf').write_text('''<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir prefix="relative">../../share/fonts</dir>
  <dir>/usr/share/fonts</dir>
  <dir>/usr/local/share/fonts</dir>
  <dir prefix="xdg">fonts</dir>
  <cachedir prefix="xdg">fontconfig</cachedir>
</fontconfig>
''')
# Preserve complete distro copyright texts, plus licenses they reference.
notices = root / 'usr/share/panelive-companion/licenses/linux-runtime'
notices.mkdir(parents=True, exist_ok=True)
versions = {}
for package in sorted(packages):
    name = package.split(':')[0]
    copyright_file = pathlib.Path('/usr/share/doc') / name / 'copyright'
    if not copyright_file.is_file():
        raise RuntimeError(f'Missing copyright: {package}')
    shutil.copy2(copyright_file, notices / (name + '.copyright'))
    versions[package] = run('dpkg-query', '-W', '-f=${Version}', package)
shutil.copytree('/usr/share/common-licenses', notices / 'common-licenses')
# Audit every shipped ELF, including the dormant OBS module, against the floor.
glibc_requirements = {}
for binary in root.rglob('*'):
    if not binary.is_file() or binary.is_symlink():
        continue
    with binary.open('rb') as stream:
        if stream.read(4) != b'\x7fELF':
            continue
    symbols = re.findall(r'\bGLIBC_([0-9]+(?:\.[0-9]+)+)\b', run('readelf', '--version-info', str(binary)))
    maximum = max((tuple(map(int, version.split('.'))) for version in symbols), default=(0,))
    if maximum > (2, 35):
        raise RuntimeError(f'{binary} exceeds the glibc 2.35 floor: {maximum}')
    glibc_requirements[str(binary.relative_to(root))] = '.'.join(map(str, maximum))
(root / 'usr/share/panelive-companion/linux-runtime.json').write_text(json.dumps({
    'build_userspace': 'Ubuntu 22.04',
    'host_libraries': host_libraries.pattern,
    'packages': versions,
    'glibc_requirements': glibc_requirements,
    'files': records,
}, indent=2) + '\n')
print(f'Bundled {len(records)} runtime files from {len(packages)} packages')
