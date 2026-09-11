"""Prepare Full/Minimal firmware downloads from completed ESP-IDF builds."""

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
           "licenses/LUA_LICENSE.txt", "components/zectrix_epd/LICENSE",
           "components/zectrix_reader/font/README.md", "components/zectrix_reader/font/OFL-1.1.txt",
           "components/zectrix_reader/third_party/miniz/LICENSE")


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
    }
    prefix = f"zectrix-note4-{version}-{profile}"
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
            f"Zectrix Note4 {version} ({profile}), ESP32-S3\n"
            f"Source: {metadata['source_commit']}\n"
            f"Firmware descriptor: {metadata['firmware_version']}\n\n"
            "Initial/recovery install: extract the complete archive, enter its directory,\n"
            "activate ESP-IDF v5.5.2, then use its esptool.py:\n"
            "  esptool.py --chip esp32s3 --port PORT write_flash @flash_args\n\n"
            "Use a data cable and replace PORT with the device's serial port.\n"
            "This rewrites the factory image, bootloader, partition table and OTA selection.\n"
            "It preserves NVS and books only on the same installed partition layout.\n"
            "Back up content before installation. Do not erase Flash or merge gaps.\n"
            "The separate app.bin is an application image, not a complete USB flash image.\n"
            "No OTA download/installation user workflow is provided by this package.\n"
            "Source and qualification notes: https://github.com/Tinnci/zectrix-note4-platform\n"
        ))
        for name in NOTICES:
            archive.write(ROOT / name, name)
        archive.write(Path(description["idf_path"]) / "LICENSE", "licenses/ESP_IDF_LICENSE.txt")
        archive.write(ROOT / "managed_components/espressif__esp_codec_dev/LICENSE",
                      "licenses/ESP_CODEC_DEV_LICENSE.txt")
    return metadata


def package(full, minimal, version, output):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,63}", version):
        raise ValueError("Version must be a filename-safe label of at most 64 characters")
    output = output.resolve()
    if output.exists():
        raise FileExistsError(f"Output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Publish the complete set only after both profiles and all writes succeed.
    with tempfile.TemporaryDirectory(dir=output.parent, prefix=".firmware-package-") as staging:
        stage = Path(staging) / "downloads"
        stage.mkdir()
        records = [package_profile(directory, profile, version, stage)
                   for profile, directory in (("full", full), ("minimal", minimal))]
        for key in ("source_commit", "source_dirty", "firmware_version", "idf_commit", "partitions"):
            if records[0][key] != records[1][key]:
                raise ValueError(f"Profiles have different {key}")
        (stage / "manifest.json").write_text(json.dumps(records, indent=2) + "\n")
        sums = "".join(f"{checksum(path)}  {path.name}\n" for path in sorted(stage.iterdir()))
        (stage / "SHA256SUMS").write_text(sums)
        stage.rename(output)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full", type=Path, default=ROOT / "build-full")
    parser.add_argument("--minimal", type=Path, default=ROOT / "build-minimal")
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        package(args.full, args.minimal, args.version, args.output)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f"Cannot package firmware: {error}", file=sys.stderr)
        return 1
    print(f"Prepared Full/Minimal archives, application images and SHA256SUMS in {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
