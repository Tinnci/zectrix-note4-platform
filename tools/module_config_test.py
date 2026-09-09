"""Exercise the Kconfig resolver used before ESP-IDF dependency expansion."""

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
                                        "READER", "BOOK_STORAGE", "USB_CLI", "UPDATE"})
        self.assertTrue(all(options.values()))
        self.assertFalse(self.config.exists())

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
                               for name in ("CONNECTIVITY", "READER", "USB_CLI", "UPDATE"))
            options = self.resolve(settings)
            self.assertTrue(all(value == enabled for value in options.values()))


if __name__ == "__main__":
    unittest.main()
