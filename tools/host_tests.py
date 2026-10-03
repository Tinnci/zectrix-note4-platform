"""Bounded macOS/Linux host test scheduling; scripts remain independently runnable."""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict, dataclass
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import threading
import time


SUITES = {
    "foundation": (
        "host-runner", "app-contract", "application-runtime", "first-party-app-controllers",
        "scene-manager", "sdk-v1", "service-registry", "runtime", "module-config",
        "localization", "input-event", "layout",
    ),
    "connectivity": (
        "companion-identity", "companion-protocol", "connectivity-policy", "connectivity-settings",
        "enrollment-ndef", "pairing-bootstrap", "radio-arbiter", "resource-client",
        "resource-gateway", "sync-engine", "wifi-backend", "wifi-http", "edge-display",
    ),
    "ui": ("display-service", "display-state", "digit-font", "reader", "reader-font", "sleep-cover", "utilities"),
    "platform": ("firmware-budget", "health-supervisor", "platform", "power-service", "storage-service",
                 "system-service", "time-service", "update-service"),
    "integration": ("book-transfer", "cli-core", "cli-diagnostics", "cli-host", "ha-bridge", "usb-manager", "zapp"),
}


@dataclass(frozen=True)
class Result:
    name: str
    status: str
    seconds: float
    exit_code: int
    log: str


class Runner:
    def __init__(self, root: Path, reports: Path, timeout: float):
        self.root, self.reports, self.timeout = root, reports, timeout
        self.cancelled = threading.Event()

    @staticmethod
    def terminate(process):
        # Kill the complete session: shells can leave compilers or HTTP servers behind.
        try:
            os.killpg(process.pid, signal.SIGTERM)
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pass
        except ProcessLookupError:
            return
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()

    def cancel(self):
        # Do not acquire worker locks from a Python signal handler.
        self.cancelled.set()

    def run(self, name, command):
        started = time.monotonic()
        log = self.reports / f"{name}.log"
        status, code = "cancelled", 130
        with log.open("wb") as output:
            if self.cancelled.is_set():
                return Result(name, status, 0, code, str(log))
            try:
                work = self.reports / "work" / name
                work.mkdir(parents=True, exist_ok=True)
                env = dict(os.environ, TMPDIR=str(work), ZECTRIX_RUNTIME_BUILD_DIR=str(work / "runtime"))
                env.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", "2")
                process = subprocess.Popen(command, cwd=self.root, stdout=output, stderr=subprocess.STDOUT,
                                           start_new_session=True, env=env)
            except OSError as error:
                output.write(str(error).encode())
                return Result(name, "failed", time.monotonic() - started, 127, str(log))
            while True:
                if self.cancelled.is_set():
                    self.terminate(process)
                    break
                if time.monotonic() - started >= self.timeout:
                    self.terminate(process)
                    status, code = "timeout", 124
                    break
                exit_code = process.poll()
                if exit_code is not None:
                    code = exit_code
                    status = "passed" if code == 0 else "failed"
                    break
                self.cancelled.wait(0.05)
        return Result(name, status, time.monotonic() - started, code, str(log))


def execute(runner, targets, jobs):
    results = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        pending = [pool.submit(runner.run, name, command) for name, command in targets]
        for future in as_completed(pending):
            result = future.result()
            results.append(result)
            print(f"{result.status.upper():9} {result.name:32} {result.seconds:7.2f}s", flush=True)
            if result.status in ("failed", "timeout"):
                print(Path(result.log).read_text(errors="replace"), flush=True)
    return results


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", action="append", choices=("all", *SUITES), help="Repeat to combine groups; default: all")
    parser.add_argument("--test", action="append", choices=tuple(name for names in SUITES.values() for name in names),
                        help="Run specific targets instead of groups; repeat as needed")
    parser.add_argument("--jobs", type=int, default=2, help="Concurrent scripts (1–8, default: 2)")
    parser.add_argument("--timeout", type=float, default=300, help="Per-target seconds (default: 300)")
    parser.add_argument("--list", action="store_true", help="List selected targets without running checks")
    parser.add_argument("--verbose", action="store_true", help="Print successful target logs too")
    parser.add_argument("--report-dir", type=Path, help="Parent for a unique run directory; default: build-host/host-tests")
    args = parser.parse_args(argv)
    if not 1 <= args.jobs <= 8 or not 0 < args.timeout < float("inf"):
        parser.error("--jobs must be 1–8 and --timeout must be finite and positive")
    if args.test and args.suite:
        parser.error("choose --test or --suite, not both")
    groups = args.suite or ["all"]
    selected = list(dict.fromkeys(args.test or [name for suite, names in SUITES.items()
                                              if "all" in groups or suite in groups for name in names]))
    if args.list:
        for suite, names in SUITES.items():
            for name in names:
                if name in selected:
                    print(f"{suite:14} test-{name}.sh")
        return 0
    root = Path(__file__).resolve().parent.parent
    parent = args.report_dir or root / "build-host" / "host-tests"
    parent.mkdir(parents=True, exist_ok=True)
    reports = Path(tempfile.mkdtemp(prefix="run-", dir=parent)).resolve()
    runner = Runner(root, reports, args.timeout)
    previous = {sig: signal.signal(sig, lambda *_: runner.cancel()) for sig in (signal.SIGINT, signal.SIGTERM)}
    started = time.monotonic()
    results = []
    try:
        # Keep existing architecture checks once, before running the actual tests.
        for name, options in (("architecture-self-test", ["--self-test"]), ("architecture", [])):
            result = runner.run(name, ["bash", str(root / "tools/check-architecture-boundaries.sh"), *options])
            results.append(result)
            print(f"{result.status.upper():9} {name:32} {result.seconds:7.2f}s", flush=True)
            if result.status != "passed":
                print(Path(result.log).read_text(errors="replace"), flush=True)
                break
        else:
            results += execute(runner, [(name, ["bash", str(root / f"tools/test-{name}.sh")]) for name in selected], args.jobs)
    finally:
        for sig, handler in previous.items():
            signal.signal(sig, handler)
    elapsed = time.monotonic() - started
    report = {"jobs": args.jobs, "elapsed_seconds": elapsed, "selected": selected,
              "results": [asdict(result) for result in results]}
    (reports / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    if args.verbose:
        for result in results:
            if result.status == "passed":
                print(f"\n--- {result.name} ---\n{Path(result.log).read_text(errors='replace')}")
    slowest = sorted(results, key=lambda result: result.seconds, reverse=True)[:5]
    print(f"Total: {elapsed:.2f}s; reports: {reports}")
    print("Slowest: " + ", ".join(f"{r.name}={r.seconds:.2f}s" for r in slowest))
    if runner.cancelled.is_set():
        return 130
    passed = len(results) == len(selected) + 2 and all(r.status == "passed" for r in results)
    print(f"{'PASS' if passed else 'FAIL'}: host suite tests={len(selected)} jobs={args.jobs}.")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
