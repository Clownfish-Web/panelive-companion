#!/usr/bin/env python3
"""Run the native refresh regression using an existing Ninja build's exact libraries.

Run in the build userspace: python3 tools/release/tests/run-linux-clients-refresh-test.py
out/build/linux-x64-jammy-release
"""
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[3]
build = pathlib.Path(sys.argv[1]).resolve()
ninja = (build / 'build.ninja').read_text()
# Limit the search to this executable's rule, before the next target.
rule = ninja[ninja.index('\nbuild panelive-companion:'):].split('\n\n', 1)[0]
libraries = re.search(r'^  LINK_LIBRARIES = (.*)$', rule, flags=re.MULTILINE).group(1)
flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'gtk4'], text=True))
with tempfile.TemporaryDirectory(prefix='panelive-refresh-test-') as temporary:
    binary = pathlib.Path(temporary) / 'refresh-test'
    subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'include'), '-I' + str(root / 'ui/linux'),
                    *flags, str(root / 'tools/release/tests/linux_clients_refresh_test.cpp'),
                    '-o', str(binary), *shlex.split(libraries)], cwd=build, check=True)
    subprocess.run(['dbus-run-session', '--', 'xvfb-run', '-a', str(binary)], check=True)
