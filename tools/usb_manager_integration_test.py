"""Run the real host client against the production CLI/USB protocol over a PTY."""

import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
import tty
import unittest

import serial

TOOL = Path(__file__).with_name("usb-manager.py")
spec = importlib.util.spec_from_file_location("usb_manager", TOOL)
usb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(usb)
BINARY, ROOT = sys.argv[1], Path(sys.argv[2])
sys.argv = sys.argv[:1]


class Device:
    def __init__(self, root):
        self.root = root
        root.mkdir(parents=True)
        master, self.slave = os.openpty()
        tty.setraw(self.slave)
        self.port = os.ttyname(self.slave)
        self.process = subprocess.Popen([BINARY, str(root)], stdin=master, stdout=master, stderr=subprocess.PIPE)
        os.close(master)
        self.transport = None
        self.client = None

    def connect(self):
        self.transport = serial.Serial(port=None, baudrate=115200, timeout=0.02, write_timeout=1)
        self.transport.dtr = self.transport.rts = False
        self.transport.port = self.port
        self.transport.open()
        self.client = usb.Client(self.transport, timeout=5)
        lines = []
        read_until = self.client._until
        def trace(*args):
            line = read_until(*args)
            lines.append(line)
            return line
        self.client._until = trace
        try:
            self.client.connect()
        except usb.HostError as error:
            raise AssertionError(f"{error}; handshake={lines[-8:]!r}; remaining={self.client.buffer!r}; process={self.process.poll()}") from error
        finally:
            self.client._until = read_until
        return self.client

    def disconnect(self):
        if self.client:
            self.client.close()
            self.client = None
        if self.transport:
            self.transport.close()
            self.transport = None

    def finish(self):
        try:
            self.disconnect()
        finally:
            self.process.terminate()
            _, stderr = self.process.communicate(timeout=5)
            os.close(self.slave)
        if self.process.returncode != 0:
            raise AssertionError(f"host simulator exit {self.process.returncode}: {stderr.decode()}")


class IntegrationTest(unittest.TestCase):
    def setUp(self):
        self.root = ROOT / self.id().split(".")[-1]
        self.root.mkdir(parents=True)
        self.device = Device(self.root / "books")
        self.addCleanup(self.device.finish)

    def test_binary_book_round_trip_and_cli(self):
        client = self.device.connect()
        source = self.root / "中文.epub"
        content = bytes(range(256)) * 512 + b"\n\x03\x04\x12host start 1\nuptime\n"
        source.write_bytes(content)
        started = time.monotonic()
        self.assertEqual(client.put(source), len(content))
        self.assertEqual((self.device.root / source.name).read_bytes(), content)
        destination = self.root / "export.epub"
        self.assertEqual(client.get(source.name, destination), len(content))
        self.assertEqual(destination.read_bytes(), content)
        self.assertEqual(list(client.books()), [{"name": source.name, "size": len(content)}])
        self.assertEqual(client.info()["upload_chunk_bytes"], 1020)
        with self.assertRaisesRegex(usb.DeviceError, "already exists"):
            client.put(source)
        with self.assertRaisesRegex(usb.HostError, "destination already exists"):
            client.get(source.name, destination)
        self.assertEqual(destination.read_bytes(), content)
        self.assertFalse(list(self.root.glob(".note4-*.part")))
        raced = self.root / "created-during-download.epub"
        def create_destination(done, total):
            if done == total:
                raced.write_bytes(b"another owner")
        with self.assertRaises(FileExistsError):
            client.get(source.name, raced, create_destination)
        self.assertEqual(raced.read_bytes(), b"another owner")
        self.assertFalse(list(self.root.glob(".note4-*.part")))
        self.device.disconnect()
        # Exercise the shipped command-line entry point, including port open,
        # version negotiation and returning the same connection to CLI mode.
        result = subprocess.run([sys.executable, str(TOOL), "--port", self.device.port, "list"],
                                check=True, capture_output=True, text=True, timeout=10)
        self.assertEqual(json.loads(result.stdout), {"name": source.name, "size": len(content)})
        print(f"USB PTY round trip: {len(content)} bytes each way in {time.monotonic() - started:.2f}s (host simulation)")

    def test_settings_and_pagination(self):
        client = self.device.connect()
        for i in range(19):
            (self.device.root / f"book-{i:02}.txt").write_text(f"Book {i}")
        self.assertEqual(len(list(client.books())), 19)
        for key, value in ((0, 1), (1, 1), (2, 2)):
            client.request(usb.SET_SETTING, struct.pack("<BI", key, value))
            self.assertEqual(client.request(usb.GET_SETTING, bytes([key])), struct.pack("<I", value))
        with self.assertRaises(usb.DeviceError):
            client.request(usb.SET_SETTING, struct.pack("<BI", 2, 3))
        with self.assertRaises(usb.DeviceError):
            client.request(usb.SET_SETTING, b"\xff\x00\x00\x00\x00")
        self.device.disconnect()
        for args, expected in ((["set-setting", "language", "en"], "Saved"),
                               (["get-setting", "language"], '{"language": "en"}')):
            result = subprocess.run([sys.executable, str(TOOL), "--port", self.device.port, *args],
                                    check=True, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.stdout.strip(), expected)

    def test_micro_app_install_export_remove_and_isolation(self):
        client = self.device.connect()
        source = TOOL.parent.parent / "apps/Calculator.lua"
        content = source.read_bytes()
        self.assertEqual(client.put(source, application=True), len(content))
        self.assertEqual((self.device.root / ".app-Calculator.lua").read_bytes(), content)
        self.assertEqual(list(client.apps()), [{"name": "Calculator.lua", "size": len(content)}])
        self.assertEqual(list(client.books()), [])
        destination = self.root / "export.lua"
        client.get(source.name, destination, application=True)
        self.assertEqual(destination.read_bytes(), content)
        with self.assertRaisesRegex(usb.DeviceError, "already exists"):
            client.put(source, application=True)
        with self.assertRaises(usb.DeviceError):
            client.put(source)
        for size, name in ((0, b"Empty.lua"), (32769, b"Big.lua"), (2, b"book.txt"), (2, b"../bad.lua")):
            with self.assertRaises(usb.DeviceError):
                client.request(usb.APP_BEGIN, struct.pack("<I", size) + name)
        client.request(usb.APP_READ_OPEN, b"Calculator.lua")
        with self.assertRaisesRegex(usb.DeviceError, "busy"):
            client.request(usb.APP_REMOVE, b"Calculator.lua")
        client.request(usb.ABORT)
        client.request(usb.APP_BEGIN, struct.pack("<I", 100) + b"Partial.lua")
        client.request(usb.CHUNK, struct.pack("<I", 0) + b"partial")
        self.device.disconnect()
        client = self.device.connect()
        self.assertFalse((self.device.root / ".upload.part").exists())
        self.assertEqual(len(list(client.apps())), 1)
        client.request(usb.APP_REMOVE, b"Calculator.lua")
        self.assertFalse((self.device.root / ".app-Calculator.lua").exists())
        self.assertEqual(list(client.apps()), [])
        self.device.disconnect()

        source = TOOL.parent.parent / "apps/Flashcards.lua"
        exported = self.root / "cards.lua"
        for command in (("app-put", str(source)), ("app-list",),
                        ("app-get", "Flashcards.lua", str(exported)), ("app-remove", "Flashcards.lua")):
            result = subprocess.run([sys.executable, str(TOOL), "--port", self.device.port, *command],
                                    check=True, capture_output=True, text=True, timeout=10)
            if command[0] == "app-list":
                self.assertEqual(json.loads(result.stdout), {"name": source.name, "size": source.stat().st_size})
        self.assertEqual(exported.read_bytes(), source.read_bytes())
        self.assertFalse((self.device.root / ".app-Flashcards.lua").exists())

    def test_cancel_and_malformed_input_recovery(self):
        client = self.device.connect()
        client.request(usb.BEGIN, struct.pack("<I", 9) + b"interrupted.txt")
        client.request(usb.CHUNK, struct.pack("<I", 0) + b"part")
        self.assertTrue((self.device.root / ".upload.part").exists())
        self.device.disconnect()
        client = self.device.connect()
        self.assertEqual(list(client.books()), [])
        self.assertFalse((self.device.root / ".upload.part").exists())
        # A malformed frame retires work, but the parser keeps owning all input
        # until an explicit Close. Embedded terminal commands cannot run.
        self.device.transport.write(b"broken\x03uptime\n\x00")
        with self.assertRaises(usb.DeviceError):
            client.request(usb.INFO)
        self.device.disconnect()
        client = self.device.connect()
        self.assertGreater(client.info()["available_bytes"], 0)

    def test_host_shutdown_aborts_staging(self):
        client = self.device.connect()
        client.request(usb.BEGIN, struct.pack("<I", 100) + b"shutdown.txt")
        client.request(usb.CHUNK, struct.pack("<I", 0) + b"part")
        self.device.process.terminate()
        self.device.process.wait(timeout=5)
        self.assertEqual(self.device.process.returncode, 0)
        self.assertFalse((self.device.root / ".upload.part").exists())
        self.assertFalse((self.device.root / "shutdown.txt").exists())
        self.device.client = None


class ClientFailureTest(unittest.TestCase):
    class Transport:
        def __init__(self, response=b""):
            self.response = response
            self.writes = []
        @property
        def in_waiting(self):
            return len(self.response)
        def read(self, size):
            data, self.response = self.response[:size], self.response[size:]
            return data
        def write(self, data):
            self.writes.append(data)
            return len(data)

    def test_uncertain_mutations_are_not_retried(self):
        for operation in (usb.COMMIT, usb.SET_SETTING, usb.APP_REMOVE):
            transport = self.Transport()
            client = usb.Client(transport, timeout=0)
            client.session = 7
            with self.assertRaisesRegex(usb.HostError, "outcome unknown"):
                client.request(operation)
            self.assertEqual(len(transport.writes), 1)

    def test_cancelled_execution_and_known_failure_are_distinct(self):
        for status in (6, 8, 9):
            response = usb.encode(usb.HEADER.pack(b"N4U1", usb.COMMIT | 0x80, status, 0, 1, 7))
            transport = self.Transport(response)
            client = usb.Client(transport, timeout=1)
            client.session = 7
            with self.assertRaises(usb.HostError) as caught:
                client.request(usb.COMMIT)
            self.assertEqual("outcome unknown" in str(caught.exception), status != 6)
            self.assertEqual(len(transport.writes), 1)

    def test_keyboard_interrupt_during_commit_reports_unknown_outcome(self):
        class Interrupted(self.Transport):
            def read(self, size):
                raise KeyboardInterrupt
        transport = Interrupted()
        client = usb.Client(transport, timeout=1)
        client.session = 7
        with self.assertRaisesRegex(usb.HostError, "outcome unknown"):
            client.request(usb.COMMIT)
        self.assertEqual(len(transport.writes), 1)


if __name__ == "__main__":
    unittest.main()
