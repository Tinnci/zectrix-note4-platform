# /// script
# requires-python = ">=3.10"
# dependencies = ["wasmtime>=36"]
# ///
"""Optional E2.1 experiments; upstream engines never enter the normal build."""

from __future__ import annotations

import argparse
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

from wasmtime import wat2wasm


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
RELEASES = {
    "wasm3": ("https://github.com/wasm3/wasm3.git", "v0.5.0"),
    "wamr": ("https://github.com/bytecodealliance/wasm-micro-runtime.git", "WAMR-2.4.5"),
    "lua": ("https://github.com/lua/lua.git", "v5.4.9"),
}


def run(command: list[str], log: Path, *, timeout: int = 600) -> None:
    with log.open("w") as stream:
        result = subprocess.run(command, cwd=ROOT, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}); see {log}")


def host_probe(engine: str, sources: Path, output: Path, sanitize: bool) -> dict:
    build = output / (engine + ("-sanitized" if sanitize else "-host"))
    flags = "-fsanitize=address,undefined -fno-omit-frame-pointer" if sanitize else ""
    run(["cmake", "-S", str(HERE), "-B", str(build),
         f"-DRESEARCH_ENGINE={engine}", f"-DRESEARCH_SOURCES={sources}",
         f"-DRESEARCH_GENERATED_DIR={output}", "-DCMAKE_BUILD_TYPE=Release",
         f"-DCMAKE_C_FLAGS={flags}", f"-DCMAKE_EXE_LINKER_FLAGS={flags}"],
        output / f"{engine}-configure.log")
    run(["cmake", "--build", str(build), "--parallel", "4"],
        output / f"{engine}-compile.log")
    binary = build / "runtime_probe"
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
    (output / f"{engine}-run.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"{engine} probe failed; see {output / (engine + '-run.log')}")
    records = [json.loads(line) for line in result.stdout.splitlines()
               if line.startswith('{"engine":')]
    if len(records) != 1:
        raise RuntimeError(f"Missing {engine} measurement")
    record = records[0]
    try:
        spin = subprocess.run([str(binary), "spin"], capture_output=True,
                              text=True, timeout=2)
    except subprocess.TimeoutExpired as error:
        captured = error.stdout or b""
        if isinstance(captured, bytes):
            captured = captured.decode("utf-8", errors="replace")
        if engine != "wasm3" or '{"event":"spin-start"}' not in captured:
            raise RuntimeError(f"Unexpected {engine} startup/execution timeout") from error
        record["spin"] = "host_process_timeout_2s"
    else:
        if spin.returncode or '{"event":"spin-trapped"}' not in spin.stdout:
            raise RuntimeError(f"{engine} spin probe failed: {spin.stdout}{spin.stderr}")
        record["spin"] = "instruction_limit" if engine == "wamr" else "count_hook_yield"
    return record


def firmware_link(engine: str, sources: Path, output: Path) -> dict:
    idf_env = os.environ.get("IDF_PYTHON_ENV_PATH")
    idf_script = shutil.which("idf.py")
    if not idf_env or not idf_script:
        raise RuntimeError("Activate tools/activate-dev-env.sh before --idf")
    python = str(Path(idf_env) / "bin/python")
    build = output / f"{engine}-full"
    idf = [python, idf_script, "--ccache", "-B", str(build)]
    run(idf + [f"-DSDKCONFIG={build / 'sdkconfig'}",
               f"-DSDKCONFIG_DEFAULTS={ROOT / 'sdkconfig.defaults'};{ROOT / 'tools/profiles/full.defaults'}",
               "-DCOMPONENTS=main;runtime_probe",
               f"-DEXTRA_COMPONENT_DIRS={HERE / 'runtime_probe'}",
               f"-DRESEARCH_ENGINE={engine}", f"-DRESEARCH_SOURCES={sources}",
               f"-DRESEARCH_GENERATED_DIR={output}", "reconfigure"],
        output / f"{engine}-idf-configure.log")
    # Link and generate the research image without flashing or altering slots.
    run(["cmake", "--build", str(build), "--target", "gen_project_binary", "--parallel", "4"],
        output / f"{engine}-idf-link.log", timeout=1200)
    run([python, "-m", "esp_idf_size", "--format", "json", "--output-file",
         str(build / "size.json"), str(build / "zectrix_epd_demo.map")],
        output / f"{engine}-idf-size.log")
    size = (build / "zectrix_epd_demo.bin").stat().st_size
    return {"image_bytes": size, "slot_bytes": 0x300000,
            "fits_existing_slot": size <= 0x300000,
            "size_report": str(build / "size.json")}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build-runtime-research")
    parser.add_argument("--sources", type=Path)
    parser.add_argument("--fetch", action="store_true", help="Clone missing upstream release sources")
    parser.add_argument("--engines", nargs="+", choices=list(RELEASES), default=list(RELEASES))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--idf", action="store_true", help="Also link each probe into an isolated Full image")
    args = parser.parse_args()
    output = args.output.resolve()
    sources = (args.sources or output / "references").resolve()
    output.mkdir(parents=True, exist_ok=True)
    sources.mkdir(parents=True, exist_ok=True)
    guest = wat2wasm((HERE / "guest.wat").read_text())
    (output / "guest.wasm").write_bytes(guest)
    (output / "guest.h").write_text(
        "#pragma once\n#include <stdint.h>\nstatic const uint8_t guest_bytes[] = {\n" +
        ",".join(str(value) for value in guest) + "\n};\n")
    report = {"host": platform.platform(), "wat_compiler": importlib.metadata.version("wasmtime"),
              "guest_bytes": len(guest), "sanitizers": args.sanitize, "engines": []}
    for engine in args.engines:
        repo, tag = RELEASES[engine]
        source = sources / engine
        if not source.exists() and args.fetch:
            run(["git", "-c", "advice.detachedHead=false", "clone", "--depth", "1",
                 "--branch", tag, repo, str(source)], output / f"{engine}-fetch.log")
        if not source.is_dir():
            raise RuntimeError(f"Missing {source}; use --fetch or --sources")
        print(f"Running {engine} ({tag}) Host experiments", flush=True)
        record = host_probe(engine, sources, output, args.sanitize)
        record["source_version"] = subprocess.check_output(
            ["git", "-C", str(source), "describe", "--tags", "--always", "--dirty"], text=True).strip()
        if args.idf:
            print(f"Linking {engine} into an isolated Full image", flush=True)
            record["full_link"] = firmware_link(engine, sources, output)
        report["engines"].append(record)
        print(json.dumps(record, ensure_ascii=False), flush=True)
    destination = output / ("sanitized-results.json" if args.sanitize else "results.json")
    destination.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Measurements: {destination}", flush=True)


if __name__ == "__main__":
    main()
