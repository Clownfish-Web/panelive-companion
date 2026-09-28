#!/usr/bin/env python3
"""Record the exact build inputs and downloadable bytes without claiming runtime validation."""
import datetime
import hashlib
import json
import os
import pathlib
import subprocess
import sys

assets, appdir = map(pathlib.Path, sys.argv[1:])
def output(*args):
    return subprocess.check_output(args, text=True).strip()
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
manifest = json.loads(pathlib.Path('vcpkg.json').read_text())
paths = set(output('git', 'ls-files').splitlines())
paths.update(output('git', 'ls-files', '--others', '--exclude-standard').splitlines())
source_hashes = {p: digest(pathlib.Path(p)) for p in sorted(paths) if pathlib.Path(p).is_file()}
result = {
    'version': manifest['version-semver'],
    'built_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'architecture': 'x86_64',
    'build_userspace': 'Ubuntu 22.04',
    'build_image_id': os.environ.get('PANELIVE_BUILD_IMAGE_ID'),
    'source_commit': output('git', 'rev-parse', 'HEAD'),
    'source_status': output('git', 'status', '--porcelain'),
    'source_sha256': source_hashes,
    'vcpkg_baseline': manifest['builtin-baseline'],
    'compiler': output('g++', '--version').splitlines()[0],
    'cmake': output('cmake', '--version').splitlines()[0],
    'build_packages': output('dpkg-query', '-W'),
    'bundled_runtime': json.loads((appdir / 'usr/share/panelive-companion/linux-runtime.json').read_text()),
    'artifacts': [{'file': p.name, 'bytes': p.stat().st_size, 'sha256': digest(p)}
                  for p in sorted(assets.iterdir()) if p.suffix in ('.deb', '.gz')],
    'build_checks': ['Release build with warnings as errors', 'Bundled launcher --version', 'All shipped ELF files require glibc <= 2.35'],
    'limitations': ['Requires host glibc 2.35+ and desktop services', 'No Linux 5.15 kernel VM or live production session validated by this build script'],
}
(assets / 'linux-build-manifest.json').write_text(json.dumps(result, indent=2) + '\n')
