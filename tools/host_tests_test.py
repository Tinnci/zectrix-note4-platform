import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

from host_tests import Result, Runner, SUITES, execute, main


class HostRunnerTests(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        self.runner = Runner(self.root, self.root, 5)

    def run_command(self, name, code):
        return self.runner.run(name, [sys.executable, "-c", code])

    def test_inventory_is_unique_and_scripts_exist(self):
        names = [name for group in SUITES.values() for name in group]
        self.assertEqual(len(names), len(set(names)))
        root = Path(__file__).resolve().parent
        for name in names:
            self.assertTrue((root / f"test-{name}.sh").is_file(), name)

    def test_listing_does_not_run_tests_and_deduplicates_groups(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(main(["--suite", "ui", "--suite", "ui", "--list"]), 0)
        lines = output.getvalue().splitlines()
        self.assertEqual(len(lines), len(SUITES["ui"]))
        self.assertTrue(all(line.startswith("ui ") for line in lines))

    def test_invalid_arguments(self):
        for args in (["--jobs", "0"], ["--jobs", "9"], ["--timeout", "nan"],
                     ["--timeout", "inf"], ["--timeout", "-1"], ["--suite", "ui", "--test", "layout"]):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                main(args)
            self.assertEqual(raised.exception.code, 2)

    def test_status_logs_and_isolated_work(self):
        passed = self.run_command("success", "import os; print(os.environ['TMPDIR']); print('hello')")
        self.assertEqual(passed.status, "passed")
        self.assertIn(str(self.root / "work/success"), Path(passed.log).read_text())
        failed = self.run_command("failure", "import sys; print('failure details'); sys.exit(7)")
        self.assertEqual((failed.status, failed.exit_code), ("failed", 7))
        self.assertIn("failure details", Path(failed.log).read_text())
        missing = self.runner.run("missing", ["/no-such-host-test-program"])
        self.assertEqual((missing.status, missing.exit_code), ("failed", 127))
        json.dumps(passed.__dict__)

    def test_failure_does_not_skip_other_targets(self):
        targets = [("bad", [sys.executable, "-c", "raise SystemExit(3)"]),
                   ("good", [sys.executable, "-c", "print('ran')"])]
        with contextlib.redirect_stdout(io.StringIO()):
            results = execute(self.runner, targets, 1)
        self.assertEqual({r.name: r.status for r in results}, {"bad": "failed", "good": "passed"})

    def test_workers_overlap(self):
        # File handshake proves overlap without assuming a CPU speed or wall-time budget.
        commands = []
        for name, peer in (("a", "b"), ("b", "a")):
            code = ("from pathlib import Path; import time; "
                    f"Path('{name}.ready').touch(); "
                    f"\nwhile not Path('{peer}.ready').exists(): time.sleep(0.01)\n")
            commands.append((name, [sys.executable, "-c", code]))
        with contextlib.redirect_stdout(io.StringIO()):
            results = execute(self.runner, commands, 2)
        self.assertTrue(all(r.status == "passed" for r in results))

    def test_worker_limit_and_complete_results(self):
        lock, barrier = threading.Lock(), threading.Barrier(2, timeout=5)
        active = maximum = 0

        def run(name, _command):
            nonlocal active, maximum
            with lock:
                active += 1
                maximum = max(maximum, active)
            if name in ("a", "b"):
                barrier.wait()
            with lock:
                active -= 1
            return Result(name, "passed", 0, 0, "")

        with patch.object(self.runner, "run", side_effect=run), contextlib.redirect_stdout(io.StringIO()):
            results = execute(self.runner, [(name, []) for name in ("a", "b", "c")], 2)
        self.assertEqual(maximum, 2)
        self.assertEqual({r.name for r in results}, {"a", "b", "c"})

    def test_report_and_exit_code_on_architecture_failure(self):
        log = self.root / "failure.log"
        log.write_text("synthetic architecture failure")
        result = Result("architecture-self-test", "failed", 0, 1, str(log))
        with patch.object(Runner, "run", return_value=result), contextlib.redirect_stdout(io.StringIO()):
            code = main(["--test", "layout", "--report-dir", str(self.root)])
        self.assertEqual(code, 1)
        report = json.loads(next(self.root.glob("run-*/results.json")).read_text())
        self.assertEqual(report["selected"], ["layout"])
        self.assertEqual(report["results"][0]["status"], "failed")

    def test_timeout_terminates_descendants(self):
        self.runner.timeout = 0.5
        code = ("import subprocess, sys, time; "
                "child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)']); "
                "print(child.pid, flush=True); time.sleep(30)")
        result = self.run_command("timeout", code)
        self.assertEqual((result.status, result.exit_code), ("timeout", 124))
        # A signaled descendant can remain a zombie briefly; it must not be executing.
        pid = int(Path(result.log).read_text().strip())
        import subprocess
        state = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
        self.assertTrue(not state or state.startswith("Z"), state)

    def test_cancellation_skips_queued_work_and_stops_running_work(self):
        self.runner.cancel()
        result = self.run_command("queued", "raise AssertionError('must not run')")
        self.assertEqual(result.status, "cancelled")
        self.runner.cancelled.clear()
        timer = threading.Timer(0.2, self.runner.cancel)
        timer.start()
        try:
            result = self.run_command("active", "import time; time.sleep(30)")
        finally:
            timer.cancel()
        self.assertEqual((result.status, result.exit_code), ("cancelled", 130))


if __name__ == "__main__":
    unittest.main()
