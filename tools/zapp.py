# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Build and inspect portable Note4 Lua application packages without executing guest code."""

import argparse
import json
import os
from pathlib import Path
import struct
import sys
import tempfile

HEADER = struct.Struct("<4sHHBBHIII64s24s48s")
SOURCE_LIMIT = 32768
PACKAGE_LIMIT = HEADER.size + 128 + SOURCE_LIMIT
PERMISSIONS = {"display": 1, "input": 2}


class PackageError(ValueError):
    pass


def text_field(value, capacity, label, *, ascii_only=False):
    if not isinstance(value, str) or not value:
        raise PackageError(f"{label} must be a nonempty string")
    encoded = value.encode("utf-8")
    if len(encoded) >= capacity or any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise PackageError(f"{label} must fit {capacity - 1} UTF-8 bytes without control characters")
    if ascii_only and any(not 33 <= ord(c) <= 126 for c in value):
        raise PackageError(f"{label} must use visible ASCII characters")
    return encoded.ljust(capacity, b"\0")


def source_text(source):
    if not 0 < len(source) <= SOURCE_LIMIT:
        raise PackageError(f"Lua source must contain 1–{SOURCE_LIMIT} bytes")
    try:
        text = source.decode("utf-8")
    except UnicodeError as error:
        raise PackageError("payload must be UTF-8 Lua source, not bytecode") from error
    if any((ord(c) < 32 and c not in "\t\n\r") or ord(c) == 127 for c in text):
        raise PackageError("Lua source contains a control byte or binary chunk")


def bounded_read(path, limit):
    with Path(path).open("rb") as file:
        data = file.read(limit + 1)
    if len(data) > limit:
        raise PackageError(f"{path}: exceeds {limit} bytes")
    return data


def read_icon(path):
    data = bounded_read(path, 16384)
    cursor = 0

    def token():
        nonlocal cursor
        while cursor < len(data):
            if data[cursor] in b" \t\r\n\v\f":
                cursor += 1
            elif data[cursor] == ord("#"):
                end = data.find(b"\n", cursor)
                cursor = len(data) if end < 0 else end + 1
            else:
                break
        start = cursor
        while cursor < len(data) and data[cursor] not in b" \t\r\n\v\f#":
            cursor += 1
        return data[start:cursor]

    magic, width, height = token(), token(), token()
    if magic not in (b"P1", b"P4") or width not in (b"16", b"32") or height != width:
        raise PackageError("icon must be a 16×16 or 32×32 monochrome PBM (P1 or P4)")
    side = int(width)
    if magic == b"P4":
        if cursor == len(data) or data[cursor] not in b" \t\r\n\v\f":
            raise PackageError("missing PBM raster separator")
        cursor += 2 if data[cursor:cursor + 2] == b"\r\n" else 1
        pixels = data[cursor:]
        if len(pixels) != side * side // 8:
            raise PackageError("incorrect PBM raster length")
        return side, pixels
    pixels = bytearray(side * side // 8)
    for bit in range(side * side):
        value = token()
        if value not in (b"0", b"1"):
            raise PackageError("PBM must have exactly one 0 or 1 per pixel")
        if value == b"1":
            pixels[bit // 8] |= 0x80 >> (bit % 8)
    if token():
        raise PackageError("unexpected pixels after the PBM raster")
    return side, bytes(pixels)


def make_package(source, icon, manifest):
    source_text(source)
    quota = manifest.get("instruction_quota", 10000)
    if type(quota) is not int or not 100 <= quota <= 10000:
        raise PackageError("instruction_quota must be an integer from 100 to 10000")
    for key in ("format_version", "guest_api"):
        value = manifest.get(key, 1)
        if type(value) is not int or value != 1:
            raise PackageError(f"only {key} 1 is supported")
    permissions = manifest.get("permissions")
    if not isinstance(permissions, list) or any(not isinstance(p, str) or p not in PERMISSIONS for p in permissions):
        raise PackageError("permissions must be a list containing only 'display' and/or 'input'")
    if len(permissions) != len(set(permissions)):
        raise PackageError("duplicate permission")
    side, pixels = icon
    if side not in (16, 32) or len(pixels) != side * side // 8:
        raise PackageError("incorrect icon dimensions or length")
    return HEADER.pack(b"ZAPP", 1, 1, 1, side, sum(PERMISSIONS[p] for p in permissions),
                       quota, len(source), 0,
                       text_field(manifest.get("name"), 64, "name"),
                       text_field(manifest.get("version"), 24, "version", ascii_only=True),
                       text_field(manifest.get("author"), 48, "author")) + pixels + source


def inspect_package(data):
    if not HEADER.size + 33 <= len(data) <= PACKAGE_LIMIT:
        raise PackageError("invalid package length")
    magic, version, api, kind, side, permissions, quota, size, reserved, name, app_version, author = HEADER.unpack_from(data)
    if magic != b"ZAPP" or version != 1 or api != 1 or kind != 1 or reserved:
        raise PackageError("unsupported package format, guest API, payload kind or flags")
    if side not in (16, 32) or permissions & ~3 or not 100 <= quota <= 10000:
        raise PackageError("invalid icon size, permission declaration or instruction quota")
    offset = HEADER.size + side * side // 8
    if len(data) != offset + size:
        raise PackageError("truncated package or unexpected trailing bytes")

    def field(raw, label, ascii_only=False):
        try:
            value = raw.split(b"\0", 1)[0].decode("utf-8")
            if text_field(value, len(raw), label, ascii_only=ascii_only) != raw:
                raise PackageError(f"{label} has nonzero padding")
            return value
        except UnicodeError as error:
            raise PackageError(f"{label} is not UTF-8") from error

    source_text(data[offset:])
    return {"format_version": version, "guest_api": api, "payload": "lua-source",
            "name": field(name, "name"), "version": field(app_version, "version", True),
            "author": field(author, "author"), "permissions": [p for p, flag in PERMISSIONS.items() if permissions & flag],
            "instruction_quota": quota, "icon_size": side, "source_bytes": size, "package_bytes": len(data)}


def publish(data, destination):
    destination = Path(destination)
    name = destination.name
    if (not name.lower().endswith(".zapp") or name.lower() == ".zapp" or
            len(name.encode("utf-8")) > 47 or any(ord(c) < 32 or ord(c) == 127 or c == "\\" for c in name)):
        raise PackageError("output must have a .zapp filename of at most 47 UTF-8 bytes")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=".zapp-", delete=False) as file:
            temporary = Path(file.name)
            file.write(data)
            file.flush()
            os.fsync(file.fileno())
        os.replace(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def build_manifest(path):
    path = Path(path)
    if path.is_dir():
        path /= "app.json"
    manifest = json.loads(bounded_read(path, 8192))
    allowed = {"name", "version", "author", "entry", "icon", "permissions", "instruction_quota", "format_version", "guest_api"}
    if not isinstance(manifest, dict) or manifest.keys() - allowed:
        raise PackageError("manifest must be an object with documented app fields")
    for key in ("entry", "icon"):
        if not isinstance(manifest.get(key), str) or not manifest[key]:
            raise PackageError(f"manifest requires a {key} path")
    source = path.parent / manifest["entry"]
    if source.suffix.lower() != ".lua":
        raise PackageError("entry must name a .lua source file")
    return make_package(bounded_read(source, SOURCE_LIMIT), read_icon(path.parent / manifest["icon"]), manifest), source


def main(argv=None):
    parser = argparse.ArgumentParser(description="Package Note4 apps; Lua is compiled by the device at launch")
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="build from a JSON manifest or a directory containing app.json")
    build.add_argument("manifest", type=Path, nargs="?", default=Path("."))
    build.add_argument("-o", "--output", type=Path)
    pack = commands.add_parser("pack", help="package source, PBM icon and explicit metadata")
    pack.add_argument("source", type=Path)
    pack.add_argument("--name", required=True)
    pack.add_argument("--version", required=True)
    pack.add_argument("--author", required=True)
    pack.add_argument("--icon", required=True, type=Path)
    pack.add_argument("--permission", choices=PERMISSIONS, action="append", default=[])
    pack.add_argument("--quota", type=int, default=10000)
    pack.add_argument("-o", "--output", type=Path)
    commands.add_parser("inspect", help="validate a package and print its metadata as JSON").add_argument("package", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "inspect":
            metadata = inspect_package(bounded_read(args.package, PACKAGE_LIMIT))
        else:
            if args.command == "build":
                data, source = build_manifest(args.manifest)
            else:
                source = args.source
                data = make_package(bounded_read(source, SOURCE_LIMIT), read_icon(args.icon), {
                    "name": args.name, "version": args.version, "author": args.author,
                    "permissions": args.permission, "instruction_quota": args.quota})
            metadata = inspect_package(data)
            output = args.output or source.parent / "dist" / (source.stem + ".zapp")
            publish(data, output)
            metadata["output"] = str(output)
        print(json.dumps(metadata, ensure_ascii=False))
        return 0
    except (PackageError, OSError, UnicodeError, json.JSONDecodeError) as error:
        print(f"zapp: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
