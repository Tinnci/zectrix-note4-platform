# /// script
# requires-python = ">=3.11"
# dependencies = ["pyserial==3.5"]
# ///
"""Note4 USB book and settings tool. Run with uv run --script tools/usb-manager.py."""

import argparse
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import time

import serial
from serial.tools import list_ports

HEADER = struct.Struct("<4sBBHII")
PAYLOAD_SIZE = 1024
WIRE_SIZE = 1046
INFO, LIST, READ_OPEN, READ, BEGIN, CHUNK, COMMIT, ABORT, GET_SETTING, SET_SETTING, CLOSE = range(1, 12)
STATUS = ("ok", "invalid request", "busy", "unavailable", "book already exists", "book not found",
          "not enough space", "storage I/O failed", "session cancelled", "session timed out",
          "applied for this boot, but not saved; repeat the setting to retry")
SETTINGS = {"language": 0, "auto_showcase": 1, "sleep_cover": 2}
VALUES = {"language": {"en": 0, "zh-CN": 1}, "auto_showcase": {"off": 0, "on": 1},
          "sleep_cover": {"dashboard": 0, "landscape": 1, "blank": 2}}


class HostError(Exception):
    pass


class DeviceError(HostError):
    def __init__(self, status):
        self.status = status
        super().__init__(STATUS[status] if status < len(STATUS) else f"device status {status}")


def encode(data):
    output = bytearray(b"\x00")
    code_at, code = 0, 1
    for value in data:
        if value == 0:
            output[code_at] = code
            code_at, code = len(output), 1
            output.append(0)
        else:
            output.append(value)
            code += 1
            if code == 255:
                output[code_at] = code
                code_at, code = len(output), 1
                output.append(0)
    output[code_at] = code
    output.append(0)
    return output


def decode(data):
    output = bytearray()
    cursor = 0
    while cursor < len(data):
        code = data[cursor]
        cursor += 1
        if not code or cursor + code - 1 > len(data):
            raise HostError("malformed USB frame")
        block = data[cursor:cursor + code - 1]
        if 0 in block:
            raise HostError("invalid USB frame delimiter")
        output.extend(block)
        cursor += code - 1
        if code < 255 and cursor < len(data):
            output.append(0)
    if len(output) > HEADER.size + PAYLOAD_SIZE:
        raise HostError("oversized USB frame")
    return output


class Client:
    def __init__(self, transport, timeout=35):
        self.transport = transport
        self.timeout = timeout
        self.session = 0
        self.request_id = 0
        self.buffer = bytearray()

    def _until(self, delimiter, limit, deadline):
        while True:
            index = self.buffer.find(delimiter)
            if index >= 0:
                if index >= limit:
                    raise HostError("oversized USB response")
                result = bytes(self.buffer[:index])
                del self.buffer[:index + len(delimiter)]
                return result
            if len(self.buffer) >= limit:
                raise HostError("oversized USB response")
            if time.monotonic() >= deadline:
                raise HostError("USB response timed out")
            size = min(4096, max(1, self.transport.in_waiting))
            self.buffer.extend(self.transport.read(size))

    def _write(self, data):
        if self.transport.write(data) != len(data):
            raise HostError("incomplete USB write")

    def connect(self):
        self.transport.reset_input_buffer()
        self.buffer.clear()
        deadline = time.monotonic() + self.timeout
        self._write(b"\x03")
        # The port can open before firmware/console initialization finishes.
        # Wait for CLI ownership before submitting the mode-change command.
        self._until(b"zectrix> ", 4096, deadline)
        self._write(b"host start 1\n")
        while True:
            line = self._until(b"\n", 1024, deadline).strip()
            match = re.fullmatch(rb"N4USB 1 ([1-9][0-9]*) 1024", line)
            if match:
                self.session = int(match[1])
                if self.session > 0xFFFFFFFF:
                    raise HostError("invalid USB session")
                return
            if b"error:" in line:
                raise HostError("USB management unavailable: open Tools > USB Manager on Note4 first")

    def request(self, operation, payload=b"", *, timeout=None):
        limit = 0xFFFFFFFF if operation == CLOSE else 0xFFFFFFFE
        if not self.session or len(payload) > PAYLOAD_SIZE or self.request_id >= limit:
            raise HostError("invalid or exhausted USB session")
        self.request_id += 1
        packet = HEADER.pack(b"N4U1", operation, 0, len(payload), self.request_id, self.session) + payload
        try:
            self._write(encode(packet))
            deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
            while True:
                raw = self._until(b"\x00", WIRE_SIZE, deadline)
                if not raw:
                    continue
                reply = decode(raw)
                if len(reply) < HEADER.size:
                    raise HostError("short USB response")
                magic, command, status, size, request_id, session = HEADER.unpack_from(reply)
                if magic != b"N4U1" or size != len(reply) - HEADER.size or session != self.session:
                    raise HostError("invalid USB response")
                # Close can follow a timeout or a partially written frame. Drain
                # the old result without retrying the abandoned operation.
                if operation == CLOSE and request_id < self.request_id:
                    continue
                if command == 0x80 and request_id == 0:
                    raise DeviceError(status)
                if command != operation | 0x80 or request_id != self.request_id:
                    raise HostError("unexpected USB response")
                if status:
                    raise DeviceError(status)
                return bytes(reply[HEADER.size:])
        except (HostError, serial.SerialException, OSError, KeyboardInterrupt) as error:
            if operation in (COMMIT, SET_SETTING) and (
                    not isinstance(error, DeviceError) or error.status in (8, 9)):
                detail = str(error) or "operation interrupted"
                raise HostError(f"{detail}; outcome unknown, inspect the book/setting before repeating") from error
            raise

    def close(self):
        if self.session:
            try:
                self._write(b"\x00")
                self.request(CLOSE, timeout=2)
            finally:
                self.session = 0

    def info(self):
        data = self.request(INFO)
        if len(data) != 16:
            raise HostError("invalid storage information")
        total, used, available, chunk = struct.unpack("<IIII", data)
        return {"total_bytes": total, "used_bytes": used, "available_bytes": available, "upload_chunk_bytes": chunk}

    def books(self):
        after = b""
        while True:
            data = self.request(LIST, after)
            if len(data) < 2 or data[0] > 8 or data[1] > 1:
                raise HostError("invalid book list")
            count, more = data[:2]
            cursor = 2
            for _ in range(count):
                if cursor + 5 > len(data):
                    raise HostError("truncated book list")
                length = data[cursor]
                size = struct.unpack_from("<I", data, cursor + 1)[0]
                cursor += 5
                name = data[cursor:cursor + length]
                if not 0 < length <= 63 or len(name) != length or name <= after:
                    raise HostError("invalid book list cursor")
                cursor += length
                after = name
                yield {"name": name.decode("utf-8"), "size": size}
            if cursor != len(data) or (more and not count):
                raise HostError("invalid book list page")
            if not more:
                return

    def put(self, source, name=None, progress=None):
        source = Path(source)
        name = (name if name is not None else source.name).encode("utf-8")
        if not 0 < len(name) <= 63 or not source.is_file():
            raise HostError("use a regular file and a book name of at most 63 UTF-8 bytes")
        with source.open("rb") as file:
            size = os.fstat(file.fileno()).st_size
            if size > 0xFFFFFFFF:
                raise HostError("book exceeds the device size limit")
            self.request(BEGIN, struct.pack("<I", size) + name)
            offset = 0
            while offset < size:
                chunk = file.read(min(PAYLOAD_SIZE - 4, size - offset))
                if not chunk:
                    raise HostError("source file changed during upload")
                reply = self.request(CHUNK, struct.pack("<I", offset) + chunk)
                offset += len(chunk)
                if reply != struct.pack("<I", offset):
                    raise HostError("unexpected upload offset")
                if progress:
                    progress(offset, size)
            if file.read(1):
                raise HostError("source file grew during upload")
            self.request(COMMIT)
        return size

    def get(self, name, destination, progress=None):
        destination = Path(destination)
        if destination.exists():
            raise HostError("destination already exists")
        data = self.request(READ_OPEN, name.encode("utf-8"))
        if len(data) != 4:
            raise HostError("invalid book size")
        size = struct.unpack("<I", data)[0]
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=".note4-", suffix=".part", delete=False) as file:
                temporary = Path(file.name)
                offset = 0
                while offset < size:
                    chunk = self.request(READ, struct.pack("<I", offset))
                    if len(chunk) != min(PAYLOAD_SIZE, size - offset):
                        raise HostError("invalid download length")
                    file.write(chunk)
                    offset += len(chunk)
                    if progress:
                        progress(offset, size)
                file.flush()
                os.fsync(file.fileno())
            # Link within the destination directory publishes a complete file
            # without replacing a name created while the download was running.
            os.link(temporary, destination)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
        return size


def main():
    parser = argparse.ArgumentParser(description="Manage Note4 books and settings over USB Serial/JTAG")
    parser.add_argument("--port", help="serial device (use 'ports' to list)")
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("ports", "info", "list"):
        commands.add_parser(command)
    put = commands.add_parser("put", help="import a TXT/EPUB book without overwriting")
    put.add_argument("source", type=Path)
    put.add_argument("--name")
    get = commands.add_parser("get", help="export a book to a new local file")
    get.add_argument("name")
    get.add_argument("destination", type=Path)
    for command in ("get-setting", "set-setting"):
        settings = commands.add_parser(command)
        settings.add_argument("key", choices=SETTINGS)
        if command == "set-setting":
            settings.add_argument("value", help="en/zh-CN, off/on, or dashboard/landscape/blank")
    args = parser.parse_args()
    if args.command == "ports":
        for port in list_ports.comports():
            print(f"{port.device}\t{port.description}")
        return 0
    if not args.port:
        parser.error("--port is required; use 'ports' to list serial devices")
    if args.command == "set-setting" and args.value not in VALUES[args.key]:
        parser.error(f"{args.key} accepts: {', '.join(VALUES[args.key])}")
    client = None
    try:
        transport = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2)
        # Avoid deliberate reset/bootloader line sequences on opening.
        transport.dtr = False
        transport.rts = False
        transport.port = args.port
        with transport:
            client = Client(transport)
            try:
                client.connect()
                if args.command == "info":
                    print(json.dumps(client.info(), ensure_ascii=False))
                elif args.command == "list":
                    for book in client.books():
                        print(json.dumps(book, ensure_ascii=False))
                elif args.command in ("put", "get"):
                    start = time.monotonic()
                    def progress(done, total):
                        print(f"\r{done}/{total} bytes", end="", file=sys.stderr, flush=True)
                    size = client.put(args.source, args.name, progress) if args.command == "put" else client.get(args.name, args.destination, progress)
                    seconds = time.monotonic() - start
                    print(f"\nCompleted {size} bytes in {seconds:.2f}s", file=sys.stderr)
                elif args.command == "get-setting":
                    data = client.request(GET_SETTING, bytes([SETTINGS[args.key]]))
                    if len(data) != 4:
                        raise HostError("invalid setting response")
                    value = struct.unpack("<I", data)[0]
                    label = next((key for key, item in VALUES[args.key].items() if item == value), value)
                    print(json.dumps({args.key: label}, ensure_ascii=False))
                else:
                    client.request(SET_SETTING, struct.pack("<BI", SETTINGS[args.key], VALUES[args.key][args.value]))
                    print("Saved")
            finally:
                try:
                    client.close()
                except (HostError, serial.SerialException, OSError) as error:
                    print(f"USB close failed: {error}; reconnect the cable before reopening the terminal", file=sys.stderr)
        return 0
    except KeyboardInterrupt:
        print("\nCancelled", file=sys.stderr)
        return 130
    except (HostError, serial.SerialException, OSError, UnicodeError) as error:
        print(f"USB: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
