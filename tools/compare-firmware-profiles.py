"""Compare fresh Full/Minimal ESP32-S3 build artifacts without a saved baseline."""

import argparse
import json
import sys
from pathlib import Path


MODULES = ("CONNECTIVITY", "WIFI", "WIFI_HTTP", "BOOK_TRANSFER", "READER", "UI_CHINESE",
           "USB_CLI", "UPDATE", "BOOK_STORAGE")
OPTIONAL_COMPONENTS = {
    "zectrix_connectivity", "zectrix_companion", "zectrix_nfc_service",
    "zectrix_reader", "zectrix_cli", "zectrix_update", "bt", "esp_wifi",
    "esp_http_client", "esp_http_server", "esp_netif", "lwip", "spiffs",
    "esp-tls", "tcp_transport",
}
CORE_SOURCES = {
    "main/app_launcher.cc", "main/app_clock.cc", "main/app_sleep_cover.cc",
    "main/application_modules.cc", "components/zectrix_app/zectrix_scene_manager.cc",
    "components/zectrix_demo_ui/zectrix_view_port.cc",
    "components/zectrix_system/zectrix_boot_guard.cc",
    "components/zectrix_system/zectrix_boot_esp.cc",
    "components/zectrix_time/zectrix_time_service.cc",
}
OPTIONAL_SOURCES = {
    "main/app_connectivity.cc", "main/app_reader.cc", "main/app_book_transfer.cc",
    "components/zectrix_app/zectrix_reader_controller.cc",
    "components/zectrix_app/zectrix_book_transfer_controller.cc",
    "components/zectrix_demo_ui/zectrix_reader_ui.cc",
    "components/zectrix_demo_ui/zectrix_book_transfer_ui.cc",
    "components/zectrix_reader/zectrix_reader_font_data.S",
    "components/zectrix_connectivity/zectrix_book_web_data.S",
    "components/zectrix_storage/zectrix_book_storage.cc",
}
BOOT_OPTIONS = ("BOOTLOADER_APP_ROLLBACK_ENABLE", "BOOTLOADER_WDT_ENABLE",
                "BOOTLOADER_WDT_DISABLE_IN_USER_CODE", "BOOTLOADER_WDT_TIME_MS")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    return json.loads(path.read_text())


def inspect_profile(directory, enabled):
    name = "Full" if enabled else "Minimal"
    description = read_json(directory / "project_description.json")
    require(description["target"] == "esp32s3", f"{name}: expected ESP32-S3 artifacts")
    require(Path(description["build_dir"]).resolve() == directory,
            f"{name}: build description belongs to another directory")
    require(Path(description["config_file"]).resolve() == directory / "sdkconfig",
            f"{name}: expected an isolated profile sdkconfig")
    config = read_json(directory / "config/sdkconfig.json")
    for module in MODULES:
        require(bool(config.get(f"ZECTRIX_ENABLE_{module}", False)) == enabled,
                f"{name}: unexpected {module} selection")
    for option in BOOT_OPTIONS:
        require(config.get(option), f"{name}: missing boot protection: {option}")

    components = set(description["build_components"])
    wrong = OPTIONAL_COMPONENTS - components if enabled else OPTIONAL_COMPONENTS & components
    require(not wrong, f"{name}: unexpected component selection: {sorted(wrong)}")
    root = Path(description["project_path"]).resolve()
    sources = set()
    for entry in read_json(directory / "compile_commands.json"):
        source = (Path(entry["directory"]) / entry["file"]).resolve()
        if source.is_relative_to(root):
            sources.add(source.relative_to(root).as_posix())
    require(CORE_SOURCES <= sources, f"{name}: missing core sources: {sorted(CORE_SOURCES - sources)}")
    wrong = OPTIONAL_SOURCES - sources if enabled else OPTIONAL_SOURCES & sources
    if not enabled:
        wrong |= {source for source in sources
                  if any(source.startswith(f"components/{component}/")
                         for component in OPTIONAL_COMPONENTS)}
    require(not wrong, f"{name}: unexpected optional sources: {sorted(wrong)}")

    # ESP-IDF splits ESP32-S3's shared SRAM into IRAM and DIRAM. Do not count
    # the linker's DRAM alias/dummy reservation a second time.
    size = read_json(directory / "size.json")
    size_keys = ("used_dram", "used_iram", "used_diram",
                 "dram_data", "dram_bss", "diram_data", "diram_bss")
    require(all(type(size[key]) is int and size[key] >= 0 for key in size_keys),
            f"{name}: invalid ESP-IDF size report")
    metrics = {
        "application_bytes": (directory / description["app_bin"]).stat().st_size,
        "static_internal_ram_bytes": sum(size[key] for key in size_keys[:3]),
        "static_data_bss_bytes": sum(size[key] for key in size_keys[3:]),
    }
    require(all(value > 0 for value in metrics.values()), f"{name}: empty build artifacts")
    print(f"PASS: {name} modules, compiled sources and mandatory boot protection.")
    return description, config, metrics


def compare(full_dir, minimal_dir):
    full, full_config, full_metrics = inspect_profile(full_dir, True)
    minimal, minimal_config, minimal_metrics = inspect_profile(minimal_dir, False)
    for key in ("project_path", "project_name", "idf_path"):
        require(full[key] == minimal[key], f"Profiles use different {key}")
    require(all(full_config[key] == minimal_config[key] for key in BOOT_OPTIONS),
            "Profiles use different boot protection settings")
    partitions = Path("partition_table/partition-table.bin")
    require((full_dir / partitions).read_bytes() == (minimal_dir / partitions).read_bytes(),
            "Profiles must preserve the same partition table, including books and OTA")

    reductions = {key: 100 * (1 - minimal_metrics[key] / value)
                  for key, value in full_metrics.items()}
    print("\nMetric                         Full bytes  Minimal bytes  Reduction")
    for key, label in (("application_bytes", "Application firmware"),
                       ("static_internal_ram_bytes", "Static internal RAM"),
                       ("static_data_bss_bytes", "  of which data + BSS")):
        print(f"{label:30} {full_metrics[key]:10,d} {minimal_metrics[key]:14,d} {reductions[key]:9.1f}%")
    require(minimal_metrics["application_bytes"] * 10 <= full_metrics["application_bytes"] * 7,
            "Minimal firmware reduction is below the 30% task target")
    require(minimal_metrics["static_internal_ram_bytes"] < full_metrics["static_internal_ram_bytes"],
            "Minimal static internal RAM did not decrease")
    return {"target": full["target"], "full": full_metrics, "minimal": minimal_metrics,
            "reduction_percent": reductions}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("full", type=Path)
    parser.add_argument("minimal", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = compare(args.full.resolve(), args.minimal.resolve())
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(f"PASS: Minimal firmware is at least 30% smaller; static RAM decreases. Report: {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
