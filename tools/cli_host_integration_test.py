"""Exercise the real host CLI through POSIX terminals and pipes."""

import errno
import fcntl
import os
import pty
import re
import select
import signal
import subprocess
import sys
import tempfile
import termios
import time
import unittest


BINARY = os.path.abspath(sys.argv.pop(1))
PROMPT = b"\r\nzectrix> "
VERSION = b"zectrix maintenance CLI D1.2"


class Terminal:
    def __init__(self, *options, stdout=None):
        self.master, self.slave = pty.openpty()
        # macOS adds a read-only descriptor flag on its first write.
        # Prime the terminal before sampling restorable descriptor state.
        os.write(self.slave, b"x")
        os.read(self.master, 1)
        self.saved_terminal = termios.tcgetattr(self.slave)
        self.saved_flags = fcntl.fcntl(self.slave, fcntl.F_GETFL)
        self.pending = b""
        self.process = subprocess.Popen(
            [BINARY, *options],
            stdin=self.slave,
            stdout=self.slave if stdout is None else stdout,
            stderr=subprocess.PIPE,
            start_new_session=True,
        )

    def __enter__(self):
        return self

    def __exit__(self, *_):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.process.stderr.close()
        # Restore the test's terminal even if a failed assertion interrupted it.
        try:
            termios.tcsetattr(self.slave, termios.TCSANOW, self.saved_terminal)
            fcntl.fcntl(self.slave, fcntl.F_SETFL, self.saved_flags)
        except (OSError, termios.error):
            pass
        os.close(self.slave)
        if self.master is not None:
            os.close(self.master)

    def send(self, data):
        while data:
            count = os.write(self.master, data)
            data = data[count:]

    def until(self, marker, timeout=4):
        deadline = time.monotonic() + timeout
        while marker not in self.pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.master], [], [], remaining)[0]:
                raise AssertionError(f"Missing {marker!r}; received {self.pending[-2000:]!r}")
            try:
                data = os.read(self.master, 16384)
            except OSError as error:
                if error.errno != errno.EIO:
                    raise
                data = b""
            if not data:
                raise AssertionError(f"Terminal closed; received {self.pending[-2000:]!r}")
            self.pending += data
        end = self.pending.index(marker) + len(marker)
        result, self.pending = self.pending[:end], self.pending[end:]
        return result

    def command(self, data):
        self.send(data)
        # Bare prompts also appear during ANSI redraws, before command execution.
        return self.until(PROMPT)

    def wait_for_raw_mode(self):
        deadline = time.monotonic() + 3
        while termios.tcgetattr(self.slave)[3] & termios.ICANON:
            if time.monotonic() >= deadline or self.process.poll() is not None:
                raise AssertionError("Host did not enter raw terminal mode")
            time.sleep(0.01)

    def assert_restored(self, test):
        restored = termios.tcgetattr(self.slave)
        saved = list(self.saved_terminal)
        # macOS sets PENDIN when restoring canonical mode, even without input.
        # It is kernel input state, not a terminal setting the client can restore.
        restored[3] &= ~getattr(termios, "PENDIN", 0)
        saved[3] &= ~getattr(termios, "PENDIN", 0)
        test.assertEqual(restored, saved)
        test.assertEqual(fcntl.fcntl(self.slave, fcntl.F_GETFL), self.saved_flags)

    def finish(self, test, expected=0):
        test.assertEqual(self.process.wait(timeout=3), expected)
        test.assertEqual(self.process.stderr.read(), b"")
        self.assert_restored(test)


class HostIntegrationTest(unittest.TestCase):
    def test_commands_editing_and_parser_recovery(self):
        with Terminal("--log-interval-ms", "0") as terminal:
            self.assertIn(b"hardware snapshots are synthetic", terminal.until(PROMPT))
            info = terminal.command(b"sysinfx\x7fo\r")
            self.assertIn(b"project=zectrix-host-sim", info)
            self.assertIn(b"chip=SIMULATED-ESP32-S3", info)
            self.assertIn(b"wifi_mac=02:00:00:00:00:01", info)
            for command in (b"hep\x1b[Da\r", b"\x1b[A\r"):
                heap = terminal.command(command)
                self.assertIn(b"internal heap bytes:", heap)
                self.assertIn(b"psram heap bytes:", heap)
            tasks = terminal.command(b"tasks\r")
            self.assertIn(b"tasks=2 capacity=32", tasks)
            self.assertIn(b"application", tasks)
            self.assertRegex(terminal.command(b"uptime\r"), rb"uptime=.*\(\d+ ms\)")
            display = terminal.command(b"epd-inspect\r")
            self.assertIn(b"panel=400x300", display)
            self.assertIn(b"framebuffer: bpp=1 bytes=15000 valid=1", display)
            self.assertRegex(display, rb"0030:(?: [0-9a-f]{2}){16}\r\n")
            self.assertIn(b"system heap", terminal.command(b"help system heap\r"))
            for command, error in (
                (b'help "unfinished\r', b"unterminated quote"),
                (b"heap extra\r", b"invalid arguments"),
                (b"missing-command\r", b"unknown command"),
                (b"x" * 65 + b"\r", b"token too long"),
                (b"help " + b"x " * 12 + b"\r", b"too many arguments"),
                (b"version\x00unexpected\r", b"invalid input"),
                (b"version\x1b[\r", b"invalid escape"),
                (b"version\x1b[" + b"1" * 40 + b"~\r", b"invalid escape"),
            ):
                with self.subTest(command=command):
                    reply = terminal.command(command)
                    self.assertIn(b"error: " + error, reply)
                    self.assertNotIn(VERSION, reply)
            # Truncating this line would execute a valid command prefix.
            overlong = terminal.command(b"version" + b" " * (256 - 7) + b"unexpected\r")
            self.assertIn(b"\a", overlong)
            self.assertIn(b"error: line too long", overlong)
            self.assertNotIn(VERSION, overlong)
            self.assertIn(b"^C", terminal.command(b"heap\x1b[\x03"))
            self.assertIn(VERSION, terminal.command(b"version\r"))
            terminal.send(b"\x04")
            terminal.finish(self)

    def test_log_filtering_and_overflow(self):
        with Terminal("--log-interval-ms", "0", "--log-burst", "80") as terminal:
            terminal.until(PROMPT)
            # An owner response also proves that startup log generation finished.
            terminal.command(b"uptime\r")
            stats = terminal.command(b"log stats\r")
            self.assertIn(b"queued=32/32", stats)
            self.assertGreater(int(re.search(rb"dropped=(\d+)", stats)[1]), 0)
            terminal.send(b"log-stream warn\r")
            stream = terminal.until(b"event=80")
            self.assertIn(b"log: dropped=", stream)
            self.assertIn(b"W host:", stream)
            self.assertNotIn(b"I host:", stream)
            self.assertIn(b"^C", terminal.command(b"\x03"))
            self.assertIn(b"queued=0/32", terminal.command(b"log stats\r"))

    def test_log_stream_keeps_owner_refreshing(self):
        with Terminal("--log-interval-ms", "20") as terminal:
            terminal.until(PROMPT)
            before = terminal.command(b"epd-inspect\r")
            initial = int(re.search(rb"attempts=(\d+)", before)[1])
            terminal.send(b"log follow info\r")
            deadline = time.monotonic() + 4
            while True:
                line = terminal.until(b"\r\n", timeout=max(0, deadline - time.monotonic()))
                refresh = re.search(rb"refresh=(\d+)", line)
                if refresh and int(refresh[1]) >= initial + 2:
                    break
            terminal.command(b"\x03")
            after = terminal.command(b"epd-inspect\r")
            self.assertGreater(int(re.search(rb"attempts=(\d+)", after)[1]), initial)

    def test_reconnect_cancel_and_exit_with_delayed_owner(self):
        with Terminal("--owner-delay-ms", "5000", "--log-interval-ms", "0") as terminal:
            terminal.until(PROMPT)
            terminal.send(b"heap\r")
            terminal.until(b"heap\r\n")
            terminal.send(b"\x12")
            reconnected = terminal.until(PROMPT, timeout=2)
            self.assertIn(b"Zectrix maintenance CLI", reconnected)
            self.assertNotIn(b"heap bytes:", reconnected)
            self.assertIn(VERSION, terminal.command(b"version\r"))
            terminal.send(b"sysinfo\r")
            terminal.until(b"sysinfo\r\n")
            terminal.send(b"\x03")
            self.assertIn(b"^C", terminal.until(PROMPT, timeout=2))
            self.assertIn(VERSION, terminal.command(b"version\r"))
            terminal.send(b"tasks\r")
            terminal.until(b"tasks\r\n")
            terminal.send(b"\x04")
            terminal.finish(self)

    def test_signals_restore_terminal_and_flags(self):
        for number in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            with self.subTest(signal=number):
                with Terminal("--owner-delay-ms", "60000") as terminal:
                    terminal.until(PROMPT)
                    terminal.send(b"heap\r")
                    terminal.until(b"heap\r\n")
                    terminal.process.send_signal(number)
                    terminal.finish(self, 128 + number)

    def test_terminal_hangup_stops_delayed_owner(self):
        with Terminal("--owner-delay-ms", "60000") as terminal:
            terminal.until(PROMPT)
            terminal.send(b"heap\r")
            terminal.until(b"heap\r\n")
            os.close(terminal.master)
            terminal.master = None
            self.assertIn(terminal.process.wait(timeout=3), (0, 1, 128 + signal.SIGHUP))

    def run_pipe(self, commands, *options):
        result = subprocess.run(
            [BINARY, "--log-interval-ms", "0", *options],
            input=commands, capture_output=True, timeout=8,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, b"")
        self.assertNotIn(b"error:", result.stdout)
        return result.stdout

    def test_piped_commands_finish_in_order_at_eof(self):
        output = self.run_pipe(
            b"sysinfo\r\nheap\ntasks\nuptime\nepd-inspect\nversion",
            "--owner-delay-ms", "25",
        )
        markers = [
            b"wifi_mac=", b"internal heap bytes:", b"psram heap bytes:",
            b"tasks=2", b"uptime=", b"panel=400x300", b"0030:", VERSION,
        ]
        positions = [output.index(marker) for marker in markers]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(output.count(VERSION), 1)

    def test_pipe_eof_cancels_stream_without_losing_buffered_commands(self):
        # Exceed the transport RX capacity before EOF becomes readable.
        output = self.run_pipe(b"log-stream\n" + b"version\n" * 100 + b"heap\n")
        self.assertIn(b"Following captured logs", output)
        self.assertIn(b"^C", output)
        self.assertEqual(output.count(VERSION), 100)
        self.assertEqual(output.count(b"internal heap bytes:"), 1)
        self.assertIn(b"^C", self.run_pipe(b"log-stream"))

    def test_redirected_file_cancels_stream_and_completes_commands(self):
        with tempfile.TemporaryFile() as commands:
            commands.write(b"log-stream\n" + b"version\n" * 100 + b"heap")
            commands.seek(0)
            result = subprocess.run(
                [BINARY, "--log-interval-ms", "0"], stdin=commands,
                capture_output=True, timeout=8,
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"^C", result.stdout)
        self.assertEqual(result.stdout.count(VERSION), 100)
        self.assertIn(b"psram heap bytes:", result.stdout)

    def test_full_stdout_pipe_does_not_block_exit(self):
        read_end, write_end = os.pipe()
        try:
            os.set_blocking(write_end, False)
            # Fill the OS pipe before launch instead of assuming a pipe capacity.
            for size in (4096, 1):
                while True:
                    try:
                        os.write(write_end, b"x" * size)
                    except BlockingIOError:
                        break
            saved_flags = fcntl.fcntl(write_end, fcntl.F_GETFL)
            with Terminal("--log-interval-ms", "1", "--log-burst", "80", stdout=write_end) as terminal:
                terminal.wait_for_raw_mode()
                terminal.send(b"log-stream\r")
                time.sleep(0.2)
                terminal.send(b"\x04")
                terminal.finish(self)
                self.assertEqual(fcntl.fcntl(write_end, fcntl.F_GETFL), saved_flags)
        finally:
            os.close(write_end)
            os.close(read_end)

    def test_broken_stdout_reports_error_instead_of_sigpipe(self):
        read_end, write_end = os.pipe()
        os.close(read_end)
        try:
            result = subprocess.run(
                [BINARY], input=b"version\n", stdout=write_end,
                stderr=subprocess.PIPE, timeout=3,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"Host transport error:", result.stderr)
        finally:
            os.close(write_end)

    def test_option_validation(self):
        result = subprocess.run([BINARY, "--help"], capture_output=True, timeout=3)
        self.assertEqual(result.returncode, 0)
        self.assertIn(b"--owner-delay-ms", result.stdout)
        for options in (
            ["--missing"], ["--owner-delay-ms"], ["--owner-delay-ms", "-1"],
            ["--owner-delay-ms", "60001"], ["--log-interval-ms", "10ms"],
            ["--log-burst", "10001"], ["--log-burst", "999999999999999999999"],
        ):
            with self.subTest(options=options):
                result = subprocess.run([BINARY, *options], capture_output=True, timeout=3)
                self.assertEqual(result.returncode, 2)
                self.assertIn(b"Invalid option or value:", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
