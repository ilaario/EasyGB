#!/usr/bin/env python3
"""
Run all Game Boy test ROMs under input/test_roms and report PASS/FAIL/TIMEOUT.

A test is considered PASS when its output contains the pass pattern
(default: "passed", case-insensitive). If the pattern appears, the test process
is terminated and the runner moves to the next test.
"""

from __future__ import annotations

import argparse
import codecs
import datetime as dt
from dataclasses import dataclass
from pathlib import Path
import queue
import re
import subprocess
import sys
import threading
import time


@dataclass
class TestResult:
    rom: Path
    status: str
    duration_s: float
    reason: str
    return_code: int | None
    log_path: Path


def sanitize_name(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "_", name)


def kill_process(proc: subprocess.Popen[bytes], grace_s: float = 0.4) -> None:
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=grace_s)
        return
    except subprocess.TimeoutExpired:
        pass

    proc.kill()
    try:
        proc.wait(timeout=grace_s)
    except subprocess.TimeoutExpired:
        pass


def stream_reader(pipe, out_queue: queue.Queue[bytes | None]) -> None:
    try:
        while True:
            chunk = pipe.read(4096)
            if not chunk:
                break
            out_queue.put(chunk)
    finally:
        out_queue.put(None)


def run_single_test(
    binary: Path,
    rom: Path,
    timeout_s: float,
    pass_pattern: str,
    fail_pattern: str,
    stop_on_pass: bool,
    quiet: bool,
    log_path: Path,
) -> TestResult:
    start = time.monotonic()
    proc = subprocess.Popen(
        [str(binary), str(rom)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    assert proc.stdout is not None

    output_queue: queue.Queue[bytes | None] = queue.Queue()
    reader = threading.Thread(target=stream_reader, args=(proc.stdout, output_queue), daemon=True)
    reader.start()

    decoder = codecs.getincrementaldecoder("utf-8")("replace")
    pass_token = pass_pattern.lower()
    fail_token = fail_pattern.lower()
    window = ""
    window_keep = max(256, len(pass_token), len(fail_token))

    pass_seen = False
    fail_seen = False
    timeout_hit = False
    stream_ended = False

    try:
        with log_path.open("wb") as log_file:
            while True:
                elapsed = time.monotonic() - start
                if elapsed >= timeout_s and proc.poll() is None:
                    timeout_hit = True
                    kill_process(proc)

                try:
                    item = output_queue.get(timeout=0.05)
                except queue.Empty:
                    item = None
                    got_item = False
                else:
                    got_item = True

                if got_item:
                    if item is None:
                        stream_ended = True
                    else:
                        chunk = item
                        log_file.write(chunk)
                        if not quiet:
                            sys.stdout.buffer.write(chunk)
                            sys.stdout.buffer.flush()

                        text = decoder.decode(chunk)
                        haystack = (window + text).lower()
                        if pass_token in haystack:
                            pass_seen = True
                            if stop_on_pass and proc.poll() is None:
                                kill_process(proc)
                        if fail_token and fail_token in haystack:
                            fail_seen = True
                        window = haystack[-window_keep:]

                if stream_ended and proc.poll() is not None:
                    break

            tail = decoder.decode(b"", final=True)
            if tail:
                tail_l = tail.lower()
                if pass_token in (window + tail_l):
                    pass_seen = True
                if fail_token and fail_token in (window + tail_l):
                    fail_seen = True
                if not quiet:
                    sys.stdout.write(tail)
                    sys.stdout.flush()
                log_file.write(tail.encode("utf-8", errors="replace"))
    finally:
        if proc.poll() is None:
            kill_process(proc)
        proc.stdout.close()
        reader.join(timeout=1.0)

    rc = proc.poll()
    duration_s = time.monotonic() - start

    if pass_seen:
        status = "PASS"
        reason = f"matched '{pass_pattern}'"
    elif timeout_hit:
        status = "TIMEOUT"
        reason = f"no '{pass_pattern}' within {timeout_s:.1f}s"
    elif fail_seen:
        status = "FAIL"
        reason = f"matched '{fail_pattern}' without '{pass_pattern}'"
    elif rc not in (0, None):
        status = "ERROR"
        reason = f"exit code {rc} without '{pass_pattern}'"
    else:
        status = "FAIL"
        reason = f"finished without '{pass_pattern}'"

    return TestResult(
        rom=rom,
        status=status,
        duration_s=duration_s,
        reason=reason,
        return_code=rc,
        log_path=log_path,
    )


def resolve_path(repo_root: Path, raw: str) -> Path:
    p = Path(raw)
    if p.is_absolute():
        return p
    return (repo_root / p).resolve()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run all ROM tests and mark PASS when output contains a token.",
    )
    parser.add_argument("--bin", default="bin/easygb", help="Path to emulator binary (default: bin/easygb)")
    parser.add_argument("--tests-root", default="input/test_roms", help="Folder containing test ROMs")
    parser.add_argument("--timeout", type=float, default=20.0, help="Timeout per test in seconds")
    parser.add_argument("--pass-pattern", default="passed", help="Case-insensitive pass token")
    parser.add_argument("--fail-pattern", default="failed", help="Case-insensitive fail token")
    parser.add_argument(
        "--no-stop-on-pass",
        action="store_true",
        help="Do not terminate process immediately after matching pass token",
    )
    parser.add_argument("--stop-on-first-nonpass", action="store_true", help="Stop suite on first non-PASS result")
    parser.add_argument("--quiet", action="store_true", help="Do not stream test output live")
    parser.add_argument("--build", action="store_true", help="Build binary target with make before running tests")
    parser.add_argument("--log-dir", default="log/test_runner", help="Directory where per-test logs are stored")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    binary = resolve_path(repo_root, args.bin)
    tests_root = resolve_path(repo_root, args.tests_root)
    base_log_dir = resolve_path(repo_root, args.log_dir)

    if args.build:
        if Path(args.bin).is_absolute():
            print("Cannot build an absolute --bin path. Use a make target path like 'bin/easygb'.", file=sys.stderr)
            return 2
        print(f"[INFO] Building target {args.bin}...")
        subprocess.run(["make", args.bin], cwd=repo_root, check=True)

    if not binary.exists():
        print(f"[ERROR] Binary not found: {binary}", file=sys.stderr)
        print("Build first with 'make' or rerun with '--build'.", file=sys.stderr)
        return 2

    if not tests_root.exists():
        print(f"[ERROR] Tests folder not found: {tests_root}", file=sys.stderr)
        return 2

    roms = sorted(p for p in tests_root.rglob("*.gb") if p.is_file())
    if not roms:
        print(f"[ERROR] No .gb files found under {tests_root}", file=sys.stderr)
        return 2

    run_stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    run_log_dir = base_log_dir / run_stamp
    run_log_dir.mkdir(parents=True, exist_ok=True)

    print(f"[INFO] Binary: {binary}")
    print(f"[INFO] Tests: {len(roms)}")
    print(f"[INFO] Timeout per test: {args.timeout:.1f}s")
    print(f"[INFO] Pass pattern: '{args.pass_pattern}'")
    print(f"[INFO] Logs: {run_log_dir}")

    suite_start = time.monotonic()
    results: list[TestResult] = []
    total = len(roms)
    stop_on_pass = not args.no_stop_on_pass

    try:
        for idx, rom in enumerate(roms, start=1):
            rel = rom.relative_to(repo_root)
            log_name = f"{idx:03d}_{sanitize_name(rel.as_posix())}.log"
            log_path = run_log_dir / log_name

            print(f"\n===== [{idx}/{total}] {rel} =====")
            result = run_single_test(
                binary=binary,
                rom=rom,
                timeout_s=args.timeout,
                pass_pattern=args.pass_pattern,
                fail_pattern=args.fail_pattern,
                stop_on_pass=stop_on_pass,
                quiet=args.quiet,
                log_path=log_path,
            )
            results.append(result)
            print(
                f"[{idx}/{total}] {result.status:<7} {result.duration_s:6.2f}s  "
                f"{rel}  ({result.reason})"
            )

            if args.stop_on_first_nonpass and result.status != "PASS":
                print("[INFO] Stopping on first non-PASS as requested.")
                break
    except KeyboardInterrupt:
        print("\n[INFO] Interrupted by user.")

    elapsed = time.monotonic() - suite_start
    passes = sum(1 for r in results if r.status == "PASS")
    fails = sum(1 for r in results if r.status == "FAIL")
    timeouts = sum(1 for r in results if r.status == "TIMEOUT")
    errors = sum(1 for r in results if r.status == "ERROR")
    nonpass = [r for r in results if r.status != "PASS"]

    print("\n===== SUMMARY =====")
    print(f"Executed: {len(results)}/{total}")
    print(f"PASS:     {passes}")
    print(f"FAIL:     {fails}")
    print(f"TIMEOUT:  {timeouts}")
    print(f"ERROR:    {errors}")
    print(f"Duration: {elapsed:.2f}s")
    print(f"Logs:     {run_log_dir}")

    if nonpass:
        print("\nNon-PASS tests:")
        for r in nonpass:
            rel = r.rom.relative_to(repo_root)
            print(f"- {r.status:<7} {rel} ({r.reason})")
            print(f"  log: {r.log_path}")

    all_passed = len(results) == total and passes == total
    return 0 if all_passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
