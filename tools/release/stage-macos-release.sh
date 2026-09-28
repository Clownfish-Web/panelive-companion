#!/usr/bin/env bash
# Collect verified artifacts; GitHub upload is a separate action.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/release-environment.sh"
repository_root="$(release_repository_root)"
cd "${repository_root}"
if [[ $# -gt 0 ]]; then
  echo 'Usage: bash tools/release/stage-macos-release.sh' >&2
  [[ $# -eq 1 && "$1" == --help ]] && exit 0
  exit 1
fi
version="$(release_manifest_version "${repository_root}")"
python3 - "${repository_root}" "${version}" <<'PY'
import hashlib
from pathlib import Path
import shutil
import sys
import tempfile

root = Path(sys.argv[1])
version = sys.argv[2]
assets = []
for architecture in ("arm64", "x86_64"):
    name = f"panelive-companion-{version}-macos-{architecture}"
    for directory, extension in (("installer", "pkg"), ("package", "zip")):
        artifact = root / "out" / directory / f"{name}.{extension}"
        manifest = Path(str(artifact) + ".sha256")
        if artifact.is_symlink() or not artifact.is_file():
            raise SystemExit(f"Missing regular artifact: {artifact}")
        digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
        if manifest.read_text().strip() != f"{digest}  {artifact.name}":
            raise SystemExit(f"Checksum mismatch: {artifact}")
        assets.append((artifact, digest))

parent = root / "out/release" / version
parent.mkdir(parents=True, exist_ok=True)
destination = parent / "macos"
if destination.is_symlink():
    raise SystemExit(f"Refusing release directory symlink: {destination}")
destination.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix=".macos-assets-", dir=parent) as temporary:
    stage = Path(temporary)
    for artifact, digest in assets:
        target = stage / artifact.name
        shutil.copyfile(artifact, target)
        if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
            raise SystemExit(f"Copy verification failed: {artifact}")
    (stage / "SHA256SUMS-macos").write_text("".join(
        f"{digest}  {artifact.name}\n" for artifact, digest in assets))
    for artifact in stage.iterdir():
        target = destination / artifact.name
        if target.is_symlink():
            raise SystemExit(f"Refusing release asset symlink: {target}")
        artifact.replace(target)
print(f"GitHub release assets: {destination}")
print("See release notes for signing status. Notarization is a separate step.")
PY
