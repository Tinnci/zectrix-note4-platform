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


def loop_probe(engine: str, binary: Path, case: str) -> str:
    marker = "spin" if case == "spin" else "init"
    log = binary.parent / f"{case}.log"
    try:
        result = subprocess.run([str(binary), case], capture_output=True,
                                text=True, timeout=2)
    except subprocess.TimeoutExpired as error:
        captured = error.stdout or b""
        errors = error.stderr or b""
        if isinstance(captured, bytes):
            captured = captured.decode("utf-8", errors="replace")
        if isinstance(errors, bytes):
            errors = errors.decode("utf-8", errors="replace")
        log.write_text(captured + errors + "Host process stopped after 2 seconds.\n")
        expected = engine == "wasm3" or (engine == "wamr" and case != "spin")
        if not expected or f'{{"event":"{marker}-start"}}' not in captured:
            raise RuntimeError(f"Unexpected {engine} {case} timeout; see {log}") from error
        return "host_process_timeout_2s"
    log.write_text(result.stdout + result.stderr)
    done = "spin-trapped" if case == "spin" else "init-stopped"
    if result.returncode or f'{{"event":"{done}"}}' not in result.stdout:
        raise RuntimeError(f"{engine} {case} probe failed; see {log}")
    return "instruction_limit" if engine == "wamr" else "count_hook_yield"


def host_probe(engine: str, sources: Path, output: Path, sanitize: bool) -> dict:
    build = output / (engine + ("-sanitized" if sanitize else "-host"))
    build.mkdir(parents=True, exist_ok=True)
    flags = ("-fsanitize=address,undefined -fno-sanitize-recover=all "
             "-fno-omit-frame-pointer") if sanitize else ""
    run(["cmake", "-S", str(HERE), "-B", str(build),
         f"-DRESEARCH_ENGINE={engine}", f"-DRESEARCH_SOURCES={sources}",
         f"-DRESEARCH_GENERATED_DIR={output}", "-DCMAKE_BUILD_TYPE=Release",
         f"-DCMAKE_C_FLAGS={flags}", f"-DCMAKE_EXE_LINKER_FLAGS={flags}"],
        build / "configure.log")
    run(["cmake", "--build", str(build), "--parallel", "4"],
        build / "compile.log")
    binary = build / "runtime_probe"
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
    (build / "run.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"{engine} probe failed; see {build / 'run.log'}")
    records = [json.loads(line) for line in result.stdout.splitlines()
               if line.startswith('{"engine":')]
    if len(records) != 1:
        raise RuntimeError(f"Missing {engine} measurement")
    record = records[0]
    record["spin"] = loop_probe(engine, binary, "spin")
    record["initialization"] = loop_probe(engine, binary, "start")
    if engine == "wamr":
        record["post_instantiate"] = loop_probe(engine, binary, "post")
    return record


def firmware_link(engine: str, sources: Path, output: Path) -> dict:
    idf_env = os.environ.get("IDF_PYTHON_ENV_PATH")
    idf_script = shutil.which("idf.py")
    if not idf_env or not idf_script:
        raise RuntimeError("Activate tools/activate-dev-env.sh before --idf")
    python = str(Path(idf_env) / "bin/python")
    build = output / f"{engine}-full"
    build.mkdir(parents=True, exist_ok=True)
    idf = [python, idf_script, "--ccache", "-B", str(build)]
    run(idf + [f"-DSDKCONFIG={build / 'sdkconfig'}",
               f"-DSDKCONFIG_DEFAULTS={ROOT / 'sdkconfig.defaults'};{ROOT / 'tools/profiles/full.defaults'}",
               "-DCOMPONENTS=main;runtime_probe",
               f"-DEXTRA_COMPONENT_DIRS={HERE / 'runtime_probe'}",
               f"-DRESEARCH_ENGINE={engine}", f"-DRESEARCH_SOURCES={sources}",
               f"-DRESEARCH_GENERATED_DIR={output}", "reconfigure"],
        build / "configure.log")
    # Link and generate the research image without flashing or altering slots.
    run(["cmake", "--build", str(build), "--target", "gen_project_binary", "--parallel", "4"],
        build / "link.log", timeout=1200)
    run([python, "-m", "esp_idf_size", "--format", "json", "--output-file",
         str(build / "size.json"), str(build / "zectrix_epd_demo.map")],
        build / "size.log")
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
    generated = ["#pragma once\n#include <stdint.h>\n"]
    guest_sizes = {}
    for name in ("guest", "guest_start", "guest_post"):
        guest = wat2wasm((HERE / f"{name}.wat").read_text())
        (output / f"{name}.wasm").write_bytes(guest)
        generated.append(f"static const uint8_t {name}_bytes[] = {{\n" +
                         ",".join(str(value) for value in guest) + "\n};\n")
        guest_sizes[name] = len(guest)
    (output / "guest.h").write_text("".join(generated))
    report = {"host": platform.platform(), "wat_compiler": importlib.metadata.version("wasmtime"),
              "guest_bytes": guest_sizes, "sanitizers": args.sanitize, "engines": [],
              "complete": False}
    destination = output / ("sanitized-results.json" if args.sanitize else "results.json")
    destination.write_text(json.dumps(report, indent=2) + "\n")
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
        destination.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(record, ensure_ascii=False), flush=True)
    report["complete"] = True
    destination.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Measurements: {destination}", flush=True)


if __name__ == "__main__":
    main()
