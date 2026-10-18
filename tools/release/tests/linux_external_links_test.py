"""Exercise GTK link activation and the packaged launcher's browser environment."""

import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
URLS = ["https://panelive.app", "https://apps.apple.com/app/id6823878747",
        "https://play.google.com/store/apps/details?id=pl.clownfishweb.panelive"]


@unittest.skipUnless(platform.system() == "Linux", "requires Linux GTK")
class LinuxExternalLinksTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for tool in ("g++", "pkg-config", "dbus-run-session", "xvfb-run"):
            if not shutil.which(tool):
                raise unittest.SkipTest(f"requires {tool}")
        flags = subprocess.run(["pkg-config", "--cflags", "--libs", "gtk4"],
                               capture_output=True, text=True)
        if flags.returncode:
            raise unittest.SkipTest("requires GTK4 development files")
        cls.build = tempfile.TemporaryDirectory(prefix="panelive-link-build-")
        cls.addClassCleanup(cls.build.cleanup)
        source = Path(cls.build.name) / "links.cpp"
        source.write_text(r'''
#include "linux_ui_helpers.hpp"
#include <cstring>
#include <vector>
int main(int argc, char** argv)
{
  gtk_init();
  auto* window = GTK_WINDOW(gtk_window_new());
  auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(window, box);
  std::vector<GtkWidget*> links;
  for (int i = 1; i < argc; ++i)
  {
    auto* link = studio_remote::linux::ui::external_link(argv[i]);
    gtk_box_append(GTK_BOX(box), link);
    links.push_back(link);
  }
  gtk_window_present(window);
  for (auto* link : links)
    g_signal_emit_by_name(link, "clicked");
  const bool failure = g_getenv("TEST_EXPECT_ERROR");
  const auto deadline = g_get_monotonic_time() + 10000000;
  while (g_get_monotonic_time() < deadline)
  {
    while (g_main_context_iteration(nullptr, FALSE)) {}
    bool finished = true;
    for (auto* link : links)
      finished = finished && gtk_link_button_get_visited(GTK_LINK_BUTTON(link));
    if (failure)
    {
      finished = false;
      auto* windows = gtk_window_get_toplevels();
      for (guint i = 0; i < g_list_model_get_n_items(windows); ++i)
      {
        auto* item = g_list_model_get_item(windows, i);
        finished = finished || GTK_IS_MESSAGE_DIALOG(item);
        g_object_unref(item);
      }
    }
    if (finished)
    {
      if (g_getenv("PANELIVE_LINUX_LAUNCHER"))
      {
        // Browser cleanup must never mutate the running application's environment.
        if (!g_str_has_suffix(g_getenv("LD_LIBRARY_PATH"), "/usr/lib")) return 3;
        if (std::strcmp(g_getenv("GTK_IM_MODULE"), "simple") != 0) return 4;
      }
      gtk_window_destroy(window);
      return 0;
    }
    g_usleep(10000);
  }
  return 2;
}
''')
        cls.binary = Path(cls.build.name) / "panelive-companion"
        subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        "-Wno-deprecated-declarations", "-I", str(ROOT / "ui/linux"),
                        str(source), str(ROOT / "ui/linux/linux_external_links.cpp"),
                        *shlex.split(flags.stdout), "-o", str(cls.binary)],
                       check=True, capture_output=True, text=True)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="panelive-links-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.env = {key: value for key, value in os.environ.items()
                    if not key.startswith(("PANELIVE_", "GTK_", "GDK_", "GIO_", "XDG_"))}
        for key in ("LD_LIBRARY_PATH", "GSETTINGS_SCHEMA_DIR", "FONTCONFIG_FILE", "GSK_RENDERER"):
            self.env.pop(key, None)
        self.env.update(HOME=str(self.root), XDG_CONFIG_HOME=str(self.root / "config"),
                        XDG_DATA_HOME=str(self.root / "data"), XDG_CACHE_HOME=str(self.root / "cache"),
                        XDG_DATA_DIRS="/usr/local/share:/usr/share", GTK_A11Y="none",
                        TEST_CAPTURE=str(self.root / "browser.jsonl"))
        applications = self.root / "data/applications"
        applications.mkdir(parents=True)
        browser = self.root / "browser"
        browser.write_text("#!/usr/bin/python3\nimport json,os,sys\n"
                           "with open(os.environ['TEST_CAPTURE'], 'a') as output:\n"
                           " output.write(json.dumps({'args':sys.argv[1:], 'env':dict(os.environ)})+'\\n')\n")
        browser.chmod(0o755)
        (applications / "test-browser.desktop").write_text(
            "[Desktop Entry]\nType=Application\nName=Test Browser\n"
            f"Exec={browser} %u\nMimeType=x-scheme-handler/https;\n")
        config = self.root / "config"
        config.mkdir()
        (config / "mimeapps.list").write_text(
            "[Default Applications]\nx-scheme-handler/https=test-browser.desktop;\n")

    def launch(self, command, urls=URLS):
        result = subprocess.run(["dbus-run-session", "--", "xvfb-run", "-a", *command, *urls],
                                env=self.env, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        if self.env.get("TEST_EXPECT_ERROR"):
            return []
        # The GIO completion signals process launch, not completion of the browser.
        import time
        capture = Path(self.env["TEST_CAPTURE"])
        for _ in range(100):
            records = [json.loads(line) for line in capture.read_text().splitlines()] if capture.exists() else []
            if len(records) == len(urls):
                break
            time.sleep(0.02)
        self.assertCountEqual([record["args"] for record in records], [[url] for url in urls])
        return records

    def test_packaged_links_restore_desktop_environment(self):
        app = self.root / "app"
        (app / "usr/bin").mkdir(parents=True)
        (app / "usr/lib").mkdir()
        shutil.copy2(ROOT / "packaging/linux/launch.sh", app / "launch.sh")
        (app / "usr/bin/panelive-companion").symlink_to(self.binary)
        (app / "usr/bin/gdk-pixbuf-query-loaders").symlink_to("/bin/true")
        self.env.update(FONTCONFIG_FILE="/etc/fonts/fonts.conf", GTK_IM_MODULE="",
                        GSK_RENDERER="cairo", GTK_MODULES="")
        records = self.launch(["sh", str(app / "launch.sh")])
        for record in records:
            env = record["env"]
            for key in ("LD_LIBRARY_PATH", "GSETTINGS_SCHEMA_DIR", "GIO_MODULE_DIR",
                        "GIO_EXTRA_MODULES", "GDK_PIXBUF_MODULEDIR", "GDK_PIXBUF_MODULE_FILE",
                        "GTK_EXE_PREFIX", "GTK_DATA_PREFIX", "GTK_PATH"):
                self.assertNotIn(key, env)
            for key in ("XDG_DATA_DIRS", "FONTCONFIG_FILE", "GTK_IM_MODULE", "GSK_RENDERER", "GTK_MODULES"):
                self.assertEqual(env[key], self.env[key])
            self.assertFalse(any(key.startswith("PANELIVE_") for key in env))

    def test_native_links_keep_environment(self):
        self.env["GTK_IM_MODULE"] = "simple"
        for record in self.launch([str(self.binary)]):
            self.assertEqual(record["env"]["GTK_IM_MODULE"], "simple")
            self.assertEqual(record["env"]["XDG_DATA_DIRS"], self.env["XDG_DATA_DIRS"])

    def test_launch_failure_is_visible(self):
        self.env["TEST_EXPECT_ERROR"] = "1"
        self.launch([str(self.binary)], ["panelive-test-missing-handler://example"])


if __name__ == "__main__":
    unittest.main()
