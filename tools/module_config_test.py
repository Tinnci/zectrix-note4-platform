"""Exercise Kconfig resolution and safe, reproducible firmware profiles."""

import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class ModuleConfigTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.config = self.root / "sdkconfig"
        self.output = self.root / "features.cmake"

    def resolve(self, settings=None, defaults=()):
        if settings is not None:
            self.config.write_text(settings)
        command = [sys.executable, str(ROOT / "cmake/configure_features.py"),
                   "--kconfig", str(ROOT / "main/Kconfig.projbuild"),
                   "--config", str(self.config), "--output", str(self.output)]
        for index, content in enumerate(defaults):
            path = self.root / f"defaults-{index}"
            path.write_text(content)
            command.extend(("--defaults", str(path)))
        subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
        if settings is not None:
            self.assertEqual(self.config.read_text(), settings)
        return {name.removeprefix("ZECTRIX_ENABLE_"): value == "1"
                for name, value in re.findall(r"set\(CONFIG_(\w+) ([01])\)", self.output.read_text())}

    def test_fresh_full_build(self):
        options = self.resolve()
        self.assertEqual(set(options), {"CONNECTIVITY", "WIFI", "WIFI_HTTP", "BOOK_TRANSFER",
                                        "READER", "BOOK_STORAGE", "USB_CLI", "USB_HOST", "UPDATE", "UI_CHINESE"})
        self.assertTrue(all(options.values()))
        self.assertFalse(self.config.exists())

    def test_committed_full_and_minimal_profiles(self):
        base = (ROOT / "sdkconfig.defaults").read_text()
        for profile, enabled in (("full", True), ("minimal", False)):
            with self.subTest(profile=profile):
                overlay = (ROOT / f"tools/profiles/{profile}.defaults").read_text()
                options = self.resolve(defaults=(base, overlay))
                self.assertTrue(all(value == enabled for value in options.values()))

    def test_profile_clean_does_not_follow_a_symlink(self):
        scripts = self.root / "tools"
        scripts.mkdir()
        script = scripts / "build-firmware.sh"
        script.write_text((ROOT / "tools/build-firmware.sh").read_text())
        developer = self.root / "developer build"
        developer.mkdir()
        saved = developer / "sdkconfig"
        saved.write_text("CONFIG_ZECTRIX_ENABLE_READER=y\n")
        # A cached build must also be rejected before idf.py fullclean runs.
        (developer / "CMakeCache.txt").write_text("existing developer cache\n")
        (self.root / "build-minimal").symlink_to(developer, target_is_directory=True)
        result = subprocess.run(["bash", str(script), "--profile", "minimal", "--clean"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("symlinked profile directory", result.stderr)
        self.assertEqual(saved.read_text(), "CONFIG_ZECTRIX_ENABLE_READER=y\n")
        self.assertTrue((developer / "CMakeCache.txt").exists())

    def test_parent_disables_requested_network_children(self):
        options = self.resolve("CONFIG_ZECTRIX_ENABLE_CONNECTIVITY=n\n"
                               "CONFIG_ZECTRIX_ENABLE_WIFI=y\n"
                               "CONFIG_ZECTRIX_ENABLE_WIFI_HTTP=y\n"
                               "CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER=y\n")
        for name in ("CONNECTIVITY", "WIFI", "WIFI_HTTP", "BOOK_TRANSFER"):
            self.assertFalse(options[name])
        self.assertTrue(options["READER"] and options["BOOK_STORAGE"])

    def test_wifi_can_be_disabled_without_removing_companion(self):
        options = self.resolve("CONFIG_ZECTRIX_ENABLE_WIFI=n\n"
                               "CONFIG_ZECTRIX_ENABLE_WIFI_HTTP=y\n"
                               "CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER=y\n")
        self.assertTrue(options["CONNECTIVITY"] and options["READER"])
        self.assertFalse(options["WIFI"] or options["WIFI_HTTP"] or options["BOOK_TRANSFER"])

    def test_web_library_is_independent_of_reader_and_https(self):
        options = self.resolve("CONFIG_ZECTRIX_ENABLE_READER=n\n"
                               "CONFIG_ZECTRIX_ENABLE_WIFI_HTTP=n\n")
        self.assertFalse(options["READER"] or options["WIFI_HTTP"])
        self.assertTrue(options["WIFI"] and options["BOOK_TRANSFER"] and options["BOOK_STORAGE"])
        options = self.resolve("CONFIG_ZECTRIX_ENABLE_READER=n\n"
                               "CONFIG_ZECTRIX_ENABLE_USB_HOST=n\n"
                               "CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER=n\n")
        self.assertFalse(options["BOOK_STORAGE"])
        self.assertTrue(options["WIFI_HTTP"])

    def test_defaults_and_saved_config_precedence(self):
        defaults = ("CONFIG_ZECTRIX_ENABLE_READER=y\nCONFIG_ZECTRIX_ENABLE_UPDATE=n\n",
                    "CONFIG_ZECTRIX_ENABLE_READER=n\n")
        options = self.resolve(defaults=defaults)
        self.assertFalse(options["READER"] or options["UPDATE"])
        options = self.resolve("CONFIG_ZECTRIX_ENABLE_READER=y\n"
                               "# CONFIG_ZECTRIX_ENABLE_USB_CLI is not set\n", defaults)
        self.assertTrue(options["READER"])
        self.assertFalse(options["USB_CLI"] or options["UPDATE"])

    def test_empty_defaults_disable_modules_like_idf(self):
        defaults = ("CONFIG_ZECTRIX_ENABLE_CONNECTIVITY=\n"
                    "CONFIG_ZECTRIX_ENABLE_READER=\n"
                    "CONFIG_ZECTRIX_ENABLE_UI_CHINESE=\n"
                    "CONFIG_ZECTRIX_ENABLE_USB_CLI=\n"
                    "CONFIG_ZECTRIX_ENABLE_UPDATE=\n",)
        self.assertFalse(any(self.resolve(defaults=defaults).values()))
        options = self.resolve("CONFIG_ZECTRIX_ENABLE_READER=y\n", defaults)
        self.assertTrue(options["READER"] and options["BOOK_STORAGE"])
        self.assertFalse(options["CONNECTIVITY"] or options["USB_CLI"] or options["UPDATE"])

    def test_reconfigure_replaces_previous_dependency_selection(self):
        for enabled in (False, True, False):
            value = "y" if enabled else "n"
            settings = "".join(f"CONFIG_ZECTRIX_ENABLE_{name}={value}\n"
                               for name in ("CONNECTIVITY", "READER", "USB_CLI", "UPDATE", "UI_CHINESE"))
            options = self.resolve(settings)
            self.assertTrue(all(value == enabled for value in options.values()))

    def test_chinese_ui_does_not_require_the_reader(self):
        options = self.resolve("CONFIG_ZECTRIX_ENABLE_READER=n\nCONFIG_ZECTRIX_ENABLE_UI_CHINESE=y\n")
        self.assertFalse(options["READER"])
        self.assertTrue(options["UI_CHINESE"])

    def test_usb_books_without_reader_or_wifi(self):
        options = self.resolve("CONFIG_ZECTRIX_ENABLE_READER=n\nCONFIG_ZECTRIX_ENABLE_CONNECTIVITY=n\n")
        self.assertTrue(options["USB_HOST"] and options["BOOK_STORAGE"])
        self.assertFalse(options["READER"] or options["BOOK_TRANSFER"])
        options = self.resolve("CONFIG_ZECTRIX_ENABLE_USB_CLI=n\nCONFIG_ZECTRIX_ENABLE_USB_HOST=y\n")
        self.assertFalse(options["USB_HOST"])


if __name__ == "__main__":
    unittest.main()
