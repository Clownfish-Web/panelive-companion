#!/usr/bin/env python3
"""Inspect a native artifact and its non-system dependency closure without executing it.

System libraries live in Apple's dyld cache and are supplied by the target OS. Their API
availability is checked by the compiler; this audit does not simulate an older OS runtime.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def command(*arguments):
    return subprocess.check_output(arguments, text=True, stderr=subprocess.PIPE).strip()


def version(value):
    parts = tuple(int(part) for part in value.split("."))
    return parts + (0,) * (3 - len(parts))


def system_library(path):
    return path.startswith(("/usr/lib/", "/System/Library/"))


def verify_source_paths(path):
    if re.search(rb"/(?:Users|home)/[^/\x00\r\n]+/", path.read_bytes()):
        raise ValueError(
            f"{path}: embedded home-directory build path; rebuild the application and "
            "static dependencies with source prefix mapping")


def inspect(path, architecture, floor, exact_architecture=False):
    if exact_architecture:
        verify_source_paths(path)
    architectures = command("/usr/bin/lipo", "-archs", str(path)).split()
    if architecture not in architectures or (exact_architecture and architectures != [architecture]):
        raise ValueError(f"{path}: expected {architecture}, found {architectures}")
    commands = command("/usr/bin/otool", "-arch", architecture, "-l", str(path))
    minimums = re.findall(r"\bminos\s+([\d.]+)", commands)
    if not minimums:
        minimums = re.findall(r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([\d.]+)", commands)
    if len(minimums) != 1 or version(minimums[0]) > version(floor):
        raise ValueError(f"{path}: minimum macOS {minimums} exceeds or cannot prove floor {floor}")
    if exact_architecture and version(minimums[0]) != version(floor):
        raise ValueError(f"{path}: product minimum macOS must equal {floor}, found {minimums[0]}")
    platforms = re.findall(r"\bplatform\s+(\S+)", commands)
    if platforms and platforms != ["1"] and platforms != ["macos"]:
        raise ValueError(f"{path}: expected macOS platform, found {platforms}")
    dependencies = []
    rpaths = []
    for block in re.split(r"Load command \d+\n", commands)[1:]:
        kind = re.search(r"\bcmd (LC_\w+)", block)
        if not kind:
            continue
        if kind[1] in ("LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB",
                       "LC_LOAD_UPWARD_DYLIB"):
            dependencies.append(re.search(r"\bname (.+) \(offset \d+\)", block)[1])
        elif kind[1] == "LC_RPATH":
            rpaths.append(re.search(r"\bpath (.+) \(offset \d+\)", block)[1])
    return dict(path=str(path), architectures=architectures, minimum_macos=minimums[0],
                sdk=re.findall(r"\bsdk\s+([\d.]+)", commands), dependencies=dependencies,
                rpaths=rpaths, sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def expand_location(location, loader, executable):
    return location.replace("@loader_path", str(loader.parent)).replace(
        "@executable_path", str(executable.parent))


def audit(binary, architecture, floor, obs_app=None):
    executable = obs_app / "Contents/MacOS/OBS" if obs_app else binary
    inherited_rpaths = []
    if obs_app:
        host = inspect(executable, architecture, floor)
        inherited_rpaths = [expand_location(item, executable, executable)
                            for item in host["rpaths"]]
    pending = [(binary, inherited_rpaths, True)]
    if obs_app:
        pending.append((executable, [], False))
    records = {}
    while pending:
        path, inherited, exact = pending.pop()
        path = path.resolve(strict=True)
        if str(path) in records:
            continue
        record = inspect(path, architecture, floor, exact)
        records[str(path)] = record
        rpaths = [expand_location(item, path, executable) for item in record["rpaths"]]
        rpaths.extend(inherited)
        for dependency in record["dependencies"]:
            if system_library(dependency):
                continue
            if not obs_app:
                raise ValueError(f"Companion has a non-system dependency: {dependency}")
            if dependency.startswith("@rpath/"):
                candidates = [str(Path(root) / dependency[len("@rpath/"):]) for root in rpaths]
            else:
                candidates = [expand_location(dependency, path, executable)]
            resolved = next((candidate for candidate in candidates
                             if system_library(candidate) or Path(candidate).is_file()), None)
            if resolved is None:
                raise ValueError(f"{path}: unresolved dependency {dependency}")
            if not system_library(resolved):
                resolved_path = Path(resolved).resolve(strict=True)
                if obs_app.resolve() not in resolved_path.parents:
                    raise ValueError(f"{path}: dependency escapes OBS bundle: {resolved_path}")
                pending.append((resolved_path, rpaths, False))
    return list(records.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--architecture", choices=("arm64", "x86_64"), required=True)
    parser.add_argument("--minimum-os", default="13.0")
    parser.add_argument("--obs-app", type=Path)
    args = parser.parse_args()
    try:
        records = audit(args.binary, args.architecture, args.minimum_os, args.obs_app)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Mach-O verification failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(dict(architecture=args.architecture, deployment_floor=args.minimum_os,
                         binaries=records), indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
