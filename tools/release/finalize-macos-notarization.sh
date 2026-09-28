#!/usr/bin/env bash
# Run after Apple accepts both architectures' ZIP and PKG submissions.
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/release-environment.sh"
repository_root="$(release_repository_root)"
cd "${repository_root}"
if [[ $# -gt 0 ]]; then
  echo 'Usage: bash tools/release/finalize-macos-notarization.sh'
  echo 'Staple accepted releases, verify Gatekeeper, rebuild ZIPs and refresh release checksums.'
  echo 'Requires macOS, internet access and successful notarization; no credentials or uploads.'
  [[ $# -eq 1 && "$1" == --help ]] && exit 0
  exit 1
fi
[[ "$(uname -s)" == Darwin ]] || release_fail 'This workflow requires macOS.'
version="$(release_manifest_version "${repository_root}")"

release_step 'Verify existing artifact checksums'
python3 - "${version}" <<'PY'
import hashlib
from pathlib import Path
import sys
for architecture in ('arm64', 'x86_64'):
    name = f'panelive-companion-{sys.argv[1]}-macos-{architecture}'
    for folder, extension in (('installer', 'pkg'), ('package', 'zip')):
        artifact = Path('out') / folder / f'{name}.{extension}'
        manifest = Path(str(artifact) + '.sha256')
        if artifact.is_symlink() or manifest.is_symlink():
            raise SystemExit(f'Refusing artifact or manifest symlink: {artifact}')
        digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
        if manifest.read_text().strip() != f'{digest}  {artifact.name}':
            raise SystemExit(f'Checksum mismatch: {artifact}')
        print(f'OK {artifact}')
PY

# Work on copies. A failed staple or verification leaves original artifacts intact.
workspace="$(mktemp -d "${repository_root}/out/.notarization.XXXXXXXX")"
trap 'rm -rf -- "${workspace}"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
for architecture in arm64 x86_64; do
  name="panelive-companion-${version}-macos-${architecture}"
  installer="${workspace}/${name}.pkg"
  release_step "Staple and verify ${architecture} installer"
  /usr/bin/ditto "out/installer/${name}.pkg" "${installer}"
  /usr/sbin/pkgutil --check-signature "${installer}"
  /usr/bin/xcrun stapler staple "${installer}"
  /usr/bin/xcrun stapler validate "${installer}"
  /usr/sbin/spctl --assess --type install --verbose=4 "${installer}"

  release_step "Staple and verify ${architecture} application bundles"
  extracted="${workspace}/${architecture}"
  /usr/bin/ditto -x -k "out/package/${name}.zip" "${extracted}"
  package="${extracted}/${name}"
  for application in \
    "${package}/panelive-companion.app" \
    "${package}/installer-tools/panelive-companion-integrations.app"; do
    /usr/bin/codesign --verify --deep --strict "${application}"
    /usr/bin/xcrun stapler staple "${application}"
    /usr/bin/xcrun stapler validate "${application}"
    /usr/bin/codesign --verify --deep --strict "${application}"
    /usr/sbin/spctl --assess --type execute --verbose=4 "${application}"
  done
  # Preserve the signed resources of the integration app, including its nested plugin.
  /usr/bin/codesign --verify --strict \
    "${package}/obs-plugin/obs-plugins/panelive-companion-obs-plugin.plugin"
  /usr/bin/ditto -c -k --sequesterRsrc --keepParent \
    "${package}" "${workspace}/${name}.zip"
done

release_step 'Replace verified artifacts and regenerate checksums'
for architecture in arm64 x86_64; do
  name="panelive-companion-${version}-macos-${architecture}"
  for extension in pkg zip; do
    directory=installer
    [[ "${extension}" != zip ]] || directory=package
    mv -f "${workspace}/${name}.${extension}" "out/${directory}/${name}.${extension}"
    (
      cd "out/${directory}"
      /usr/bin/shasum -a 256 "${name}.${extension}" > "${name}.${extension}.sha256"
    )
  done
done
bash tools/release/stage-macos-release.sh
(
  cd "out/release/${version}/macos"
  /usr/bin/shasum -a 256 -c SHA256SUMS-macos
)

if [[ -d "releases/${version}" ]]; then
  cp "out/release/${version}/macos/SHA256SUMS-macos" "releases/${version}/SHA256SUMS-macos"
  python3 - "releases/${version}/NOTES-macos.md" <<'PY'
from pathlib import Path
import sys
notes = Path(sys.argv[1])
if notes.is_file():
    text = notes.read_text()
    old = ('The downloads are **not yet\n'
           'notarized**; macOS Gatekeeper may block downloaded copies until notarization is\n'
           'completed.')
    new = ('Both architectures\' installers and ZIP contents are **notarized**. Tickets are\n'
           'stapled to the PKG installers and the application bundles in the ZIPs;\n'
           'ticket validation and Gatekeeper assessments passed during release finalization.')
    if old in text:
        notes.write_text(text.replace(old, new))
PY
fi
echo "Finalized notarized downloads: ${repository_root}/out/release/${version}/macos/"
echo 'Publish these finalized files. Rebuilding packages will require finalization again.'
