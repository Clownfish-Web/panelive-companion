"""Exercise the compatibility gate with real native Mach-O files, without running them."""

import importlib.util
from pathlib import Path
import platform
import shutil
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "macos_binary", ROOT / "tools/release/verify-macos-binary.py")
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


@unittest.skipUnless(platform.system() == "Darwin", "requires native Apple tools")
class MacosBinaryTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="Mach-O compatibility ")
        self.root = Path(self.temporary.name)
        self.architecture = platform.machine()
        self.source = self.root / "main.c"
        self.source.write_text("int main(void) { return 0; }\n")

    def tearDown(self):
        self.temporary.cleanup()

    def build(self, name, minimum="13.0", *options):
        binary = self.root / name
        binary.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["xcrun", "clang", "-arch", self.architecture,
                        f"-mmacosx-version-min={minimum}", str(self.source),
                        *options, "-o", str(binary)], check=True, capture_output=True)
        return binary

    def test_build_selection_rejects_unsupported_floor_and_architectures(self):
        cmake = str(ROOT / ".buildtools/python/bin/cmake")
        if not Path(cmake).is_file():
            cmake = shutil.which("cmake")
        for architecture, floor, success in [
            ("arm64", "13.0", True), ("x86_64", "13.0", True),
            ("arm64;x86_64", "13.0", False), ("x86_64h", "13.0", False),
            ("arm64", "12.0", False), ("arm64", "14.0", False),
        ]:
            with self.subTest(architecture=architecture, floor=floor):
                result = subprocess.run(
                    [cmake, f"-DCMAKE_OSX_ARCHITECTURES={architecture}",
                     f"-DCMAKE_OSX_DEPLOYMENT_TARGET={floor}", "-P",
                     str(ROOT / "cmake/platform/MacosDeployment.cmake")],
                    capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode == 0, success, result.stderr)

    def test_native_floor_and_system_linkage(self):
        binary = self.build("native")
        records = VERIFY.audit(binary, self.architecture, "13.0")
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["minimum_macos"], "13.0")
        self.assertTrue(records[0]["dependencies"])

    def test_embedded_home_paths_are_rejected(self):
        self.source.write_text(
            '#include <stdio.h>\n'
            'int main(void) { puts("/Users/build user/private-checkout/main.c"); }\n')
        binary = self.build("leaked-source-path")
        with self.assertRaisesRegex(ValueError, "embedded home-directory build path"):
            VERIFY.audit(binary, self.architecture, "13.0")

    def test_triplet_mapping_covers_file_macro_and_source_location(self):
        cmake = str(ROOT / ".buildtools/python/bin/cmake")
        if not Path(cmake).is_file():
            cmake = shutil.which("cmake")
        script = self.root / "flags.cmake"
        flags = self.root / "flags.txt"
        source = self.root / "diagnostics.cpp"
        source.write_text(
            '#include <cstdio>\n#include <source_location>\n'
            f'#line 1 "{ROOT}/private build/diagnostics.cpp"\n'
            'int main() { puts(__FILE__); puts(std::source_location::current().file_name()); }\n')
        for triplet in ("arm64-osx", "x64-osx"):
            with self.subTest(triplet=triplet):
                script.write_text(
                    f'include("{ROOT}/cmake/triplets/{triplet}.cmake")\n'
                    f'file(WRITE "{flags}" "${{VCPKG_CXX_FLAGS}}")\n')
                subprocess.run([cmake, "-P", str(script)], check=True, capture_output=True)
                binary = self.root / triplet
                subprocess.run(
                    ["xcrun", "clang++", "-std=c++20", "-mmacosx-version-min=13.0",
                     *shlex.split(flags.read_text()), str(source), "-o", str(binary)],
                    check=True, capture_output=True)
                VERIFY.audit(binary, self.architecture, "13.0")
                output = subprocess.check_output([str(binary)], text=True)
                self.assertEqual(output.splitlines(),
                                 ["/src/panelive-companion/private build/diagnostics.cpp"] * 2)

    def test_imported_library_cmake_gate(self):
        cmake = str(ROOT / ".buildtools/python/bin/cmake")
        if not Path(cmake).is_file():
            cmake = shutil.which("cmake")
        script = self.root / "check.cmake"
        script.write_text(
            f'include("{ROOT}/cmake/platform/MacosMachO.cmake")\n'
            'studio_remote_check_macos_library("${LIBRARY}")\n')
        for minimum, success in [("13.0", True), ("14.0", False)]:
            binary = self.build(f"library-{minimum}", minimum, "-dynamiclib")
            result = subprocess.run(
                [cmake, f"-DCMAKE_OSX_ARCHITECTURES={self.architecture}",
                 "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0", f"-DLIBRARY={binary}",
                 "-P", str(script)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode == 0, success, result.stderr)

    def test_newer_os_is_rejected_despite_native_architecture(self):
        binary = self.build("too-new", "14.0")
        with self.assertRaisesRegex(ValueError, "minimum macOS"):
            VERIFY.audit(binary, self.architecture, "13.0")

    def test_wrong_architecture_is_rejected(self):
        binary = self.build("wrong-slice")
        other = "x86_64" if self.architecture == "arm64" else "arm64"
        with self.assertRaisesRegex(ValueError, "expected"):
            VERIFY.audit(binary, other, "13.0")

    def test_missing_load_commands_are_not_treated_as_compatible(self):
        binary = self.root / "not-a-binary"
        binary.write_text("not Mach-O")
        with self.assertRaises(subprocess.CalledProcessError):
            VERIFY.audit(binary, self.architecture, "13.0")

    def test_obs_transitive_floor_and_unresolved_dependency(self):
        obs = self.root / "OBS.app"
        library = self.build("OBS.app/Contents/Frameworks/libfixture.dylib", "14.0",
                             "-dynamiclib", "-Wl,-install_name,@rpath/libfixture.dylib")
        host = self.build("OBS.app/Contents/MacOS/OBS", "13.0", str(library),
                          "-Wl,-rpath,@executable_path/../Frameworks")
        helper = self.build("helper", "13.0", "-bundle")
        with self.assertRaisesRegex(ValueError, "minimum macOS"):
            VERIFY.audit(helper, self.architecture, "13.0", obs)
        library.unlink()
        with self.assertRaisesRegex(ValueError, "unresolved dependency"):
            VERIFY.audit(helper, self.architecture, "13.0", obs)
        self.assertTrue(host.exists())

    def test_companion_cannot_depend_on_obs_or_checkout_library(self):
        library = self.build("private.dylib", "13.0", "-dynamiclib")
        binary = self.build("companion", "13.0", str(library))
        with self.assertRaisesRegex(ValueError, "non-system dependency"):
            VERIFY.audit(binary, self.architecture, "13.0")


if __name__ == "__main__":
    unittest.main()
