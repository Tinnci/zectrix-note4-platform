import importlib.util
from pathlib import Path
import unittest
import subprocess
import sys
import tempfile

spec = importlib.util.spec_from_file_location("migration", Path(__file__).with_name("migrate-config.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MigrationTests(unittest.TestCase):
    def test_cli_preserves_source_and_refuses_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "sdkconfig"
            output = Path(directory) / "build" / "sdkconfig"
            old = "CONFIG_ZECTRIX_ENABLE_READER=y\n"
            source.write_text(old)
            command = [sys.executable, str(Path(__file__).with_name("migrate-config.py")),
                       str(source), str(output)]
            first = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(source.read_text(), old)
            self.assertEqual(output.read_text(), "CONFIG_NOTE4_ENABLE_READER=y\n")
            output.write_text("user settings\n")
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(output.read_text(), "user settings\n")
            self.assertEqual(source.read_text(), old)

    def test_names_values_comments_and_idempotence(self):
        old = ('CONFIG_ZECTRIX_ENABLE_READER=y\n# CONFIG_ZECTRIX_ENABLE_WIFI_HTTP is not set\n'
               'CONFIG_ZECTRIX_DEMO_RF_THRESHOLD_DBM=-75\nCONFIG_LOG_DEFAULT_LEVEL=3\n'
               'CONFIG_WIFI_NAME="ZECTRIX_TEST"\n')
        fresh = module.migrate_config(old)
        self.assertIn("CONFIG_NOTE4_ENABLE_READER=y", fresh)
        self.assertIn("# CONFIG_NOTE4_ENABLE_WIFI_HTTP is not set", fresh)
        self.assertIn("CONFIG_NOTE4_QUALIFICATION_RF_THRESHOLD_DBM=-75", fresh)
        self.assertIn('CONFIG_WIFI_NAME="ZECTRIX_TEST"', fresh)
        self.assertEqual(module.migrate_config(fresh), fresh)

    def test_mixed_conflicting_values_are_not_silently_lost(self):
        with self.assertRaises(ValueError):
            module.migrate_config("CONFIG_ZECTRIX_ENABLE_READER=y\n# CONFIG_NOTE4_ENABLE_READER is not set\n")
        self.assertIn("CONFIG_NOTE4_ENABLE_READER=y", module.migrate_config(
            "CONFIG_ZECTRIX_ENABLE_READER=y\nCONFIG_NOTE4_ENABLE_READER=y\n"))


if __name__ == "__main__":
    unittest.main()
