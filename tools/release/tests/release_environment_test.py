"""Release boundary checks, runnable with Python 3 on macOS and Linux."""

import os
from pathlib import Path
import platform
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
HELPERS = ROOT / "tools/release/release-environment.sh"
MACOS_HELPERS = ROOT / "tools/release/macos-release-environment.sh"
SCRIPT = ROOT / "tools/release/build-macos-package.sh"


class ReleaseEnvironmentTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="release checks ")
        self.directory = Path(self.temporary.name)
        self.environment = {
            key: value for key, value in os.environ.items()
            if not key.startswith("PANELIVE_COMPANION_")
        }

    def tearDown(self):
        self.temporary.cleanup()

    def shell(self, command, *arguments):
        return subprocess.run(
            ["/bin/bash", "-euo", "pipefail", "-c",
             'source "$1"; source "$2"; shift 2; ' + command,
             "release-test", str(HELPERS), str(MACOS_HELPERS),
             *map(str, arguments)],
            env=self.environment, text=True, capture_output=True, timeout=10,
        )

    def test_precedence_and_literal_values(self):
        configuration = self.directory / "release.env"
        marker = self.directory / "must-not-exist"
        literal = f'$(touch "{marker}") `touch "{marker}"`'
        configuration.write_text(f"PANELIVE_COMPANION_VERSION='{literal}'\n")
        command = ('release_import_environment_file "$1" true; '
                   'release_configured_value PANELIVE_COMPANION_VERSION "${2:-}"')
        result = self.shell(command, configuration)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, literal)
        self.assertFalse(marker.exists())
        self.environment["PANELIVE_COMPANION_VERSION"] = "2.0.0"
        self.assertEqual(self.shell(command, configuration).stdout, "2.0.0")
        self.assertEqual(self.shell(command, configuration, "3.0.0").stdout, "3.0.0")

    def test_rejects_invalid_configuration(self):
        configuration = self.directory / "release.env"
        for content, message in [
            ("PANELIVE_COMPANION_VERSION=1.0.0\nPANELIVE_COMPANION_VERSION=2.0.0\n", "Duplicate"),
            ("UNRECOGNIZED=value\n", "Unknown"),
            ('PANELIVE_COMPANION_VERSION="unclosed\n', "Mismatched"),
            ("export PANELIVE_COMPANION_VERSION=1.0.0\n", "Invalid"),
        ]:
            with self.subTest(content=content):
                configuration.write_text(content)
                result = self.shell('release_import_environment_file "$1" true', configuration)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)

    def test_missing_configuration_and_empty_value(self):
        missing = self.directory / "absent.env"
        self.assertEqual(self.shell('release_import_environment_file "$1" false', missing).returncode, 0)
        self.assertNotEqual(self.shell('release_import_environment_file "$1" true', missing).returncode, 0)
        missing.write_text('PANELIVE_COMPANION_VERSION=""\n')
        result = self.shell('release_import_environment_file "$1" true; '
                            'release_configured_value PANELIVE_COMPANION_VERSION ""', missing)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "")

    def test_staging_removal_preserves_unowned_directories_and_symlinks(self):
        parent = self.directory / "packages"
        parent.mkdir()
        outside = self.directory / "unrelated"
        outside.mkdir()
        marker = outside / "keep"
        marker.write_text("retained")
        link = parent / "package"
        link.symlink_to(outside, target_is_directory=True)
        for target in [link, outside, parent]:
            result = self.shell('release_remove_owned_directory "$1" "$2"', target, parent)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(marker.exists())
        owned = parent / "owned"
        owned.mkdir()
        result = self.shell('release_remove_owned_directory "$1" "$2"', owned, parent)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(owned.exists())

    def test_cache_rejects_wrong_architecture_and_duplicate_entries(self):
        cache = self.directory / "CMakeCache.txt"
        for content in ["CMAKE_OSX_ARCHITECTURES:STRING=x86_64\n",
                        "CMAKE_OSX_ARCHITECTURES:STRING=arm64\n" * 2]:
            cache.write_text(content)
            result = self.shell('macos_assert_cache_value "$1" CMAKE_OSX_ARCHITECTURES arm64',
                                self.directory)
            self.assertNotEqual(result.returncode, 0)

    def test_cached_tool_symlink_matches_only_its_resolved_target(self):
        tool = self.directory / "Cellar/ninja/1.0/bin/ninja"
        tool.parent.mkdir(parents=True)
        tool.write_text("fixture")
        link = self.directory / "bin/ninja"
        link.parent.mkdir()
        link.symlink_to(tool)
        (self.directory / "CMakeCache.txt").write_text(
            f"CMAKE_MAKE_PROGRAM:FILEPATH={link}\n")
        command = 'macos_assert_cache_path "$1" CMAKE_MAKE_PROGRAM "$2"'
        result = self.shell(command, self.directory, tool)
        self.assertEqual(result.returncode, 0, result.stderr)
        other = self.directory / "different-ninja"
        other.write_text("different executable")
        result = self.shell(command, self.directory, other)
        self.assertNotEqual(result.returncode, 0)

    def test_installable_packages_require_a_configured_signing_identity(self):
        cache = self.directory / "CMakeCache.txt"
        for identity, accepted in [("-", False), ("", False), ("Apple Development: Test", True)]:
            with self.subTest(identity=identity):
                cache.write_text(f"STUDIO_REMOTE_MACOS_CODESIGN_IDENTITY:STRING={identity}\n")
                result = self.shell('macos_assert_team_signing_configured "$1" '
                                    'STUDIO_REMOTE_MACOS_CODESIGN_IDENTITY', self.directory)
                self.assertEqual(result.returncode == 0, accepted, result.stderr)

    @unittest.skipUnless(platform.system() == "Darwin", "requires Apple signing tools")
    def test_installable_signature_gate_rejects_ad_hoc_and_modified_code(self):
        # Copy an existing team-signed build, then alter only isolated fixture copies.
        architecture = "arm64" if platform.machine() == "arm64" else "x64"
        default_bundle = ROOT / f"out/build/macos-{architecture}-release/panelive-companion.app"
        built = Path(os.environ.get("PANELIVE_RELEASE_TEST_BUNDLE", str(default_bundle)))
        fixture = self.directory / "Companion.app"
        subprocess.run(["/usr/bin/ditto", str(built), str(fixture)], check=True)
        result = self.shell('macos_verify_team_signature "$1"', fixture)
        self.assertEqual(result.returncode, 0, result.stderr)
        subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", str(fixture)],
                       check=True, capture_output=True)
        result = self.shell('macos_verify_team_signature "$1"', fixture)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Apple-issued team signature", result.stderr)
        subprocess.run(["/usr/bin/ditto", str(built), str(fixture)], check=True)
        (fixture / "Contents/Resources/unsealed-file").write_text("tampered")
        result = self.shell('macos_verify_team_signature "$1"', fixture)
        self.assertNotEqual(result.returncode, 0)

    def test_postinstall_uses_existing_approval_and_keeps_user_work_unelevated(self):
        # Execute the unchanged production callback against fake native boundaries.
        scripts = ROOT / "packaging/macos/installer/scripts"
        callback = self.directory / "postinstall"
        callback.write_bytes((scripts / "postinstall").read_bytes())
        events = self.directory / "events"
        environment = self.directory / "installer-environment.sh"
        environment.write_text(r'''installer_context() { :; }
require_closed_applications() { :; }
as_installer_user() {
  case "$*" in
    *--detect-obs-installation*) printf '%s\n' '/Applications/OBS.app';;
    *--record-installation*) echo user-record >> "$EVENTS";;
    *--initialize-installation*) echo user-initialize >> "$EVENTS";;
    */usr/bin/open*) echo user-launch >> "$EVENTS";;
    *register_plugin_link*) echo user-register >> "$EVENTS";;
    *) echo user-prepare >> "$EVENTS";;
  esac
}
install_shared_integration() {
  [[ "$*" == "/Library/Application Support/panelive-companion/installation" ]] || return 90
  echo shared-integration >> "$EVENTS"
  return "$INTEGRATION_RESULT"
}
''')
        for status in (0, 1):
            with self.subTest(integration_status=status):
                events.write_text("")
                result = subprocess.run(
                    ["/bin/bash", str(callback), "package.pkg", "/", "/"],
                    env={**self.environment, "EVENTS": str(events),
                         "INTEGRATION_RESULT": str(status)}, capture_output=True, text=True)
                expected = ["user-prepare", "user-record", "shared-integration"]
                if status == 0:
                    expected += ["user-register", "user-initialize", "user-launch"]
                self.assertEqual(events.read_text().splitlines(), expected, result.stderr)
                self.assertEqual(result.returncode, status, result.stderr)

    def test_help_and_missing_argument_do_not_start_builds(self):
        for arguments, expected in [(["--help"], 0), (["--package-parent"], 1),
                                    (["--version", "--help"], 1), (["--unknown"], 1)]:
            result = subprocess.run(["/bin/bash", str(SCRIPT), *arguments],
                                    env=self.environment, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, expected, result.stderr)

    def test_manifest_keeps_the_prerelease_suffix(self):
        # The manifest helper reads the manifest's normally indented field.
        (self.directory / "vcpkg.json").write_text('{\n  "version-semver": "1.2.3-beta.4"\n}\n')
        result = self.shell('release_manifest_version "$1"', self.directory)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "1.2.3-beta.4")

    def test_debian_prerelease_tilde_is_literal(self):
        for source, expected in [("0.1.0-beta.1", "0.1.0~beta.1"), ("1.2.3", "1.2.3")]:
            result = self.shell('release_debian_version "$1"', source)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, expected)

    @unittest.skipUnless(platform.system() == "Darwin", "requires Apple architecture tools")
    def test_native_and_translated_target_execution_are_distinguished(self):
        command = ('macos_select_architecture "$1"; '
                   'printf "%s %s" "$macos_architecture" "$macos_verification_mode"')
        host = platform.machine()
        result = self.shell(command, host)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, f"{host} native")
        for invalid in ("x86_64h", "arm64;x86_64", "invalid"):
            self.assertNotEqual(self.shell(command, invalid).returncode, 0)
        if host == "arm64":
            translated = subprocess.run(
                ["/usr/bin/arch", "-x86_64", "/usr/sbin/sysctl", "-in",
                 "sysctl.proc_translated"], capture_output=True, text=True, timeout=10)
            result = self.shell(command, "x86_64")
            if translated.returncode == 0 and translated.stdout.strip() == "1":
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "x86_64 rosetta")
            else:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("require Rosetta", result.stderr)
        else:
            self.assertNotEqual(self.shell(command, "arm64").returncode, 0)


if __name__ == "__main__":
    unittest.main()
