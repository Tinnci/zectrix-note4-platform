"""Package ESP32-S3 profiles and assemble release downloads from matrix builds."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import zipfile

from firmware_budget import inspect_budget


ROOT = Path(__file__).resolve().parent.parent
ROLES = ("bootloader", "partition-table", "app", "otadata")
NOTICES = ("LICENSE", "THIRD_PARTY_NOTICES.md", "licenses/TRMNL_FONT_LICENSE.txt",
           "licenses/LUA_LICENSE.txt", "components/note4_epd/LICENSE",
           "components/note4_reader/font/README.md", "components/note4_reader/font/OFL-1.1.txt",
           "components/note4_reader/third_party/miniz/LICENSE")
PROFILE_NAMES = ("full", "minimal", "reader")
IDENTITY_FIELDS = ("package_version", "target", "source_commit", "source_dirty",
                   "firmware_version", "idf_commit", "partitions")


def build_file(directory, name):
    relative = PurePosixPath(name)
    path = directory / relative
    if relative.is_absolute() or ".." in relative.parts or not path.resolve().is_relative_to(directory):
        raise ValueError(f"Artifact is outside the build directory: {name}")
    if not path.is_file():
        raise ValueError(f"Missing artifact: {name}")
    return path


def checksum(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verified_downloads(directory):
    directory = directory.resolve()
    sums = build_file(directory, "SHA256SUMS")
    names = set()
    for line in sums.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([^/\\\r\n]+)", line)
        if not match:
            raise ValueError("Invalid download checksum entry")
        digest, name = match.groups()
        if name in names or name == "SHA256SUMS":
            raise ValueError(f"Duplicate download checksum: {name}")
        path = build_file(directory, name)
        if path.is_symlink() or checksum(path) != digest:
            raise ValueError(f"Download checksum mismatch: {name}")
        names.add(name)
    actual = {path.name for path in directory.iterdir()} - {"SHA256SUMS"}
    if "manifest.json" not in names or names != actual:
        raise ValueError("Download checksums must cover the complete download set")
    return sorted(names)


def package_profile(directory, profile, version, output):
    directory = directory.resolve()
    description = json.loads((directory / "project_description.json").read_text())
    flash = json.loads((directory / "flasher_args.json").read_text())
    if description["target"] != "esp32s3":
        raise ValueError("Expected ESP32-S3 firmware")
    files = {int(offset, 0): name for offset, name in flash["flash_files"].items()}
    expected = {int(flash[role]["offset"], 0): flash[role]["file"] for role in ROLES}
    if len(files) != len(flash["flash_files"]) or len(expected) != len(ROLES) or files != expected:
        raise ValueError("Flash bundle must contain only bootloader, table, application and OTA selection")
    for name in files.values():
        build_file(directory, name)
    if flash["app"]["file"] != description["app_bin"]:
        raise ValueError("Flasher and build description select different applications")
    budget = inspect_budget(directory)
    config = json.loads((directory / "config/sdkconfig.json").read_text())
    if budget["application_free_bytes"] < 0:
        raise ValueError("Application does not fit every installed application slot")
    for address, name in files.items():
        end = address + (directory / name).stat().st_size
        for partition in budget["partitions"]:
            if partition["type"] == 1 and partition["subtype"] != 0:
                if address < partition["offset"] + partition["size"] and end > partition["offset"]:
                    raise ValueError(f"Flash segment overlaps user/system data: {partition['name']}")

    provenance = dict(line.split("=", 1) for line in
                      (directory / "build-provenance.txt").read_text().splitlines() if "=" in line)
    metadata = {
        "package_version": version, "profile": profile, "target": description["target"],
        "firmware_version": description["project_version"],
        "source_commit": provenance["git_commit"], "source_dirty": bool(provenance["git_status"]),
        "idf_version": provenance["idf_version"], "idf_commit": provenance["idf_commit"],
        "flash_files": {hex(address): name for address, name in sorted(files.items())},
        "application_bytes": budget["application_bytes"],
        "application_free_bytes": budget["application_free_bytes"],
        "partitions": budget["partitions"],
        "enabled_modules": sorted(key.removeprefix("NOTE4_ENABLE_")
                                  for key, enabled in config.items()
                                  if key.startswith("NOTE4_ENABLE_") and enabled),
    }
    prefix = f"note4-{version}-{profile}"
    shutil.copyfile(directory / description["app_bin"], output / f"{prefix}-app.bin")
    # Keep separate flash segments: filling their gaps would overwrite NVS/books.
    arguments = shlex.join(flash["write_flash_args"]) + "\n"
    arguments += "".join(f"{hex(address)} {shlex.quote(name)}\n" for address, name in sorted(files.items()))
    with zipfile.ZipFile(output / f"{prefix}.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for name in files.values():
            archive.write(directory / name, name)
        archive.writestr("flash_args", arguments)
        archive.writestr("flasher_args.json", json.dumps(flash, indent=2) + "\n")
        archive.writestr("manifest.json", json.dumps(metadata, indent=2) + "\n")
        archive.writestr("README.txt", (
            f"Note4 {version} ({profile}), ESP32-S3\n"
            f"Source: {metadata['source_commit']}\n"
            f"Firmware descriptor: {metadata['firmware_version']}\n\n"
            "Initial/recovery install: extract the complete archive, enter its directory,\n"
            "install uv (https://docs.astral.sh/uv/), then run:\n"
            '  uvx --from esptool==4.11.0 esptool.py --chip esp32s3 --port PORT write_flash "@flash_args"\n\n'
            "Use a data cable and replace PORT with the device's serial port.\n"
            "This rewrites the factory image, bootloader, partition table and OTA selection.\n"
            "It preserves NVS and books only on the same installed partition layout.\n"
            "Back up content before installation. Do not erase Flash or merge gaps.\n"
            "The separate app.bin is an application image, not a complete USB flash image.\n"
            "No OTA download/installation user workflow is provided by this package.\n"
            "For a new library, use the separate library-init archive once. It replaces all content.\n"
            f"User guide: https://github.com/Tinnci/zectrix-note4-platform/blob/{version}/docs/HANDBOOK.md\n"
            f"中文手册: https://github.com/Tinnci/zectrix-note4-platform/blob/{version}/docs/HANDBOOK_zh.md\n"
            "Source and qualification notes: https://github.com/Tinnci/zectrix-note4-platform\n"
        ))
        for name in NOTICES:
            archive.write(ROOT / name, name)
        archive.write(Path(description["idf_path"]) / "LICENSE", "licenses/ESP_IDF_LICENSE.txt")
        archive.write(ROOT / "managed_components/espressif__esp_codec_dev/LICENSE",
                      "licenses/ESP_CODEC_DEV_LICENSE.txt")
    return metadata


def package_library(directory, version, output, record):
    directory = directory.resolve()
    partition = next(item for item in record["partitions"] if item["name"] == "books")
    if partition["type"] != 1 or partition["subtype"] != 0x82:
        raise ValueError("Expected the books SPIFFS partition")
    image = build_file(directory, "books.bin")
    if image.stat().st_size != partition["size"]:
        raise ValueError("Library image must match the books partition size")
    config = json.loads((directory / "config/sdkconfig.json").read_text())
    metadata = {key: record[key] for key in ("package_version", "source_commit", "source_dirty")}
    metadata.update(partition=partition, spiffs_object_name_length=config["SPIFFS_OBJ_NAME_LEN"])
    with zipfile.ZipFile(output / f"note4-{version}-library-init.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(image, "books.bin")
        archive.writestr("flash_args", f"{hex(partition['offset'])} books.bin\n")
        archive.writestr("manifest.json", json.dumps(metadata, indent=2) + "\n")
        archive.writestr("README.txt", (
            "OPTIONAL LIBRARY INITIALIZATION / 可选书库初始化\n\n"
            "CAUTION: This replaces the entire books partition, including books, apps and phone pictures.\n"
            "Use it once for a new library, or for an intentional reset after exporting your content.\n"
            "An ordinary firmware update does not need this image.\n"
            "注意：此操作替换全部书籍、应用和手机画报。仅用于新书库或备份后的主动重置。\n"
            "普通固件升级不要刷入此文件。先安装同版 Full 或 Reader 固件，再解压本文件并执行：\n\n"
            '  uvx --from esptool==4.11.0 esptool.py --chip esp32s3 --port PORT write_flash "@flash_args"\n\n'
            "Replace PORT with your Note4 serial port. / 将 PORT 替换为 Note4 串口。\n"
            "This writes the bundled reading guide. It preserves firmware and NVS settings.\n"
        ))


def validate_version(version):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,63}", version):
        raise ValueError("Version must be a filename-safe label of at most 64 characters")


def complete_set(stage, records, output):
    if not records or len({record["profile"] for record in records}) != len(records):
        raise ValueError("A download set needs unique firmware profiles")
    for record in records:
        if record["profile"] not in PROFILE_NAMES:
            raise ValueError("Unsupported firmware profile")
        for key in IDENTITY_FIELDS:
            if records[0][key] != record[key]:
                raise ValueError(f"Profiles have different {key}")
    records.sort(key=lambda record: PROFILE_NAMES.index(record["profile"]))
    (stage / "manifest.json").write_text(json.dumps(records, indent=2) + "\n")
    sums = "".join(f"{checksum(path)}  {path.name}\n" for path in sorted(stage.iterdir()))
    (stage / "SHA256SUMS").write_text(sums)
    stage.rename(output)


def package_profiles(profiles, version, output, *, library=False, extras=None):
    validate_version(version)
    if not profiles or any(name not in PROFILE_NAMES for name in profiles):
        raise ValueError("Select full, minimal or reader profiles")
    if library and "full" not in profiles:
        raise ValueError("Library initialization must use the Full build")
    output = output.resolve()
    if output.exists():
        raise FileExistsError(f"Output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Keep incomplete sets private until every profile and write succeeds.
    with tempfile.TemporaryDirectory(dir=output.parent, prefix=".firmware-package-") as staging:
        stage = Path(staging) / "downloads"
        stage.mkdir()
        records = [package_profile(directory, profile, version, stage) for profile, directory in profiles.items()]
        if library:
            package_library(profiles["full"], version, stage,
                            next(record for record in records if record["profile"] == "full"))
        if extras:
            for path in sorted(extras.iterdir()):
                if path.is_symlink() or not path.is_file() or (stage / path.name).exists() or path.name in ("manifest.json", "SHA256SUMS"):
                    raise ValueError(f"Invalid or conflicting extra download: {path.name}")
                shutil.copyfile(path, stage / path.name)
        complete_set(stage, records, output)
    return records


def package(full, minimal, version, output):
    return package_profiles({"full": full, "minimal": minimal}, version, output)


def collect_packages(directories, version, output):
    validate_version(version)
    output = output.resolve()
    if output.exists():
        raise FileExistsError(f"Output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    records = []
    with tempfile.TemporaryDirectory(dir=output.parent, prefix=".firmware-package-") as staging:
        stage = Path(staging) / "downloads"
        stage.mkdir()
        for directory in directories:
            names = verified_downloads(directory)
            incoming = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
            for record in incoming:
                if record["package_version"] != version:
                    raise ValueError("Matrix package version does not match the release")
                prefix = f"note4-{version}-{record['profile']}"
                for name in (f"{prefix}.zip", f"{prefix}-app.bin"):
                    build_file(directory.resolve(), name)
            records.extend(incoming)
            for name in names:
                if name == "manifest.json":
                    continue
                path = directory / name
                if path.is_symlink() or not path.is_file() or (stage / path.name).exists():
                    raise ValueError(f"Invalid or duplicate matrix download: {path.name}")
                shutil.copyfile(path, stage / path.name)
        complete_set(stage, records, output)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full", type=Path, default=ROOT / "build-full")
    parser.add_argument("--minimal", type=Path, default=ROOT / "build-minimal")
    parser.add_argument("--profile", action="append", metavar="NAME=BUILD_DIR",
                        help="Package a matrix profile. Repeat to package several profiles.")
    parser.add_argument("--collect", type=Path, nargs="+", help="Combine completed matrix download directories")
    parser.add_argument("--library", action="store_true", help="Add a separate, explicit library initialization archive")
    parser.add_argument("--extras", type=Path, help="Include prepared guides and tools from this directory")
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.collect:
            if args.profile or args.library or args.extras:
                raise ValueError("Collect accepts completed downloads only")
            collect_packages(args.collect, args.version, args.output)
        else:
            profiles = {"full": args.full, "minimal": args.minimal}
            if args.profile:
                profiles = {}
                for item in args.profile:
                    name, path = item.split("=", 1)
                    if name in profiles:
                        raise ValueError(f"Duplicate profile: {name}")
                    profiles[name] = Path(path)
            package_profiles(profiles, args.version, args.output, library=args.library, extras=args.extras)
    except (OSError, ValueError, KeyError, TypeError, StopIteration, subprocess.CalledProcessError) as error:
        print(f"Cannot package firmware: {error}", file=sys.stderr)
        return 1
    print(f"Prepared firmware downloads and SHA256SUMS in {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
