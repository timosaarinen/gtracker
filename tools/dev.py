#!/usr/bin/env python3
"""Small, cross-platform developer entry point. Requires Python 3.10+."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import socket
import subprocess
import sys
import time
import uuid


ROOT = Path(__file__).resolve().parents[1]
CI_INTERVAL = timedelta(hours=24)
PROFILES = ("headless", "desktop")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def utf8_stdio() -> None:
    # Redirected Windows Python defaults to the ANSI code page even when the
    # child/native JSON is UTF-8. Configure both sides of this CLI boundary.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")


def git(root: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", "-C", str(root), *args], capture_output=True, check=False
    )


def source_state(root: Path) -> dict:
    """Hash actual source bytes, including untracked source in an unborn repo."""
    listing = git(root, "ls-files", "-z", "--cached", "--others", "--exclude-standard")
    if listing.returncode:
        raise RuntimeError("Cannot fingerprint source: this directory must be a Git repository.")
    paths = sorted(set(os.fsdecode(p) for p in listing.stdout.split(b"\0") if p))
    digest = hashlib.sha256()
    for name in paths:
        path = root / name
        digest.update(os.fsencode(name) + b"\0")
        if path.is_symlink():
            digest.update(b"symlink\0" + os.fsencode(os.readlink(path)))
        elif path.is_file():
            digest.update(f"file:{path.stat().st_mode & 0o111}\0".encode())
            with path.open("rb") as source:
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    digest.update(block)
        elif path.exists():
            digest.update(b"directory\0")
        else:
            digest.update(b"deleted\0")
        digest.update(b"\0")
    head = git(root, "rev-parse", "--verify", "HEAD")
    branch = git(root, "branch", "--show-current")
    status = git(root, "status", "--porcelain=v1", "-z")
    return {
        "revision": head.stdout.decode().strip() if head.returncode == 0 else None,
        "branch": branch.stdout.decode().strip() or None,
        "dirty": bool(status.stdout),
        "fingerprint_sha256": digest.hexdigest(),
        "file_count": len(paths),
        "fingerprint_basis": "Git-listed tracked and nonignored untracked working-tree paths and bytes",
    }


def atomic_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def read_latest(root: Path) -> dict | None:
    path = root / ".local" / "ci" / "latest.json"
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def local_binding(root: Path) -> str:
    value = "\0".join((socket.gethostname(), str(root.resolve()), platform.system(), platform.machine()))
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def evaluate_status(latest: dict | None, source: dict, now: datetime | None = None, expected_binding: str | None = None) -> dict:
    now = now or datetime.now(timezone.utc)
    reasons = []
    source_matches = False
    evidence_local = False
    if not isinstance(latest, dict):
        reasons.append("No readable local CI evidence exists.")
        latest = None
    elif latest.get("status") != "passed":
        reasons.append("The latest CI attempt did not pass.")
    if latest:
        tested_source = latest.get("source_after", latest.get("source", {}))
        if not isinstance(tested_source, dict) or not tested_source.get("fingerprint_sha256"):
            reasons.append("The latest CI attempt has no valid source fingerprint.")
        else:
            source_matches = tested_source["fingerprint_sha256"] == source.get("fingerprint_sha256")
        evidence_local = expected_binding is None or latest.get("local_binding_sha256") == expected_binding
        if not evidence_local:
            reasons.append("The latest CI evidence has no matching checkout/host binding.")
        tested_platform = latest.get("platform")
        if isinstance(tested_platform, dict) and (
            tested_platform.get("system") != platform.system()
            or tested_platform.get("machine") != platform.machine()
        ):
            reasons.append("The latest CI attempt was recorded for another platform.")
        try:
            finished = datetime.fromisoformat(latest["finished_utc"])
            if finished.tzinfo is None:
                raise ValueError("Timestamp is not timezone-aware")
            age = now - finished
            if age >= CI_INTERVAL:
                reasons.append("The latest CI attempt is at least 24 hours old.")
            elif age < timedelta(minutes=-5):
                reasons.append("The recorded completion time is unexpectedly in the future.")
        except (KeyError, TypeError, ValueError):
            reasons.append("The latest CI attempt has no valid completion time.")
    if latest and latest.get("status") == "passed" and evidence_local:
        qualification = (
            "The latest local pass used these source bytes."
            if source_matches else
            "The latest local pass covers different source bytes; changed code requires focused checks."
        )
    else:
        qualification = "No passing local receipt qualifies the current source."
    return {
        "schema": "gtracker.ci-status",
        "version": 1,
        "checked_utc": now.isoformat(timespec="seconds"),
        "due": bool(reasons),
        "reasons": reasons,
        "source_matches": source_matches,
        "evidence_local": evidence_local,
        "qualification": qualification,
        "source": source,
        "latest": latest,
        "scope": "Local headless build, tests, and offline render; no GUI, listening, or capture claim.",
    }


def ci_status(root: Path) -> dict:
    return evaluate_status(read_latest(root), source_state(root), expected_binding=local_binding(root))


def pid_is_alive(pid: int) -> bool | None:
    """Unknown ownership is treated conservatively; never send a real signal."""
    if pid <= 0:
        return None
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes

        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.GetExitCodeProcess.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
        kernel.GetExitCodeProcess.restype = wintypes.BOOL
        kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
        handle = kernel.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return False if ctypes.get_last_error() == 87 else None  # ERROR_INVALID_PARAMETER
        try:
            exit_code = wintypes.DWORD()
            if not kernel.GetExitCodeProcess(handle, ctypes.byref(exit_code)):
                return None
            return exit_code.value == 259  # STILL_ACTIVE
        finally:
            kernel.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except (PermissionError, OSError):
        return None


@contextmanager
def ci_lock(root: Path):
    path = root / ".local" / "ci" / "lock.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    owner = {"pid": os.getpid(), "host": socket.gethostname(), "started_utc": utc_now(), "token": uuid.uuid4().hex}
    try:
        descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError:
        try:
            existing = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            existing = {}
        same_host = existing.get("host") == socket.gethostname()
        pid = existing.get("pid")
        dead = same_host and isinstance(pid, int) and pid_is_alive(pid) is False
        detail = (
            "The recorded same-host PID no longer exists; this appears to be a stale lock. "
            "After checking that no CI run remains, remove the lock file and retry."
            if dead else "Another CI run owns the lock, or its ownership cannot be verified."
        )
        raise RuntimeError(f"CI is locked: {path}. {detail}") from None
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        json.dump(owner, handle)
    try:
        yield
    finally:
        try:
            current = json.loads(path.read_text(encoding="utf-8"))
            if current.get("token") == owner["token"]:
                path.unlink()
        except (OSError, ValueError):
            pass


def run_command(root: Path, argv: list[str], log_path: Path | None = None, echo: bool = True, separate_stderr: bool = False,
                cancel_file: Path | None = None) -> dict:
    started = utc_now()
    clock = time.monotonic()
    if echo:
        print("+ " + subprocess.list2cmdline(argv), flush=True)
    log = log_path.open("w", encoding="utf-8") if log_path else None
    code = 127
    try:
        process = subprocess.Popen(argv, cwd=root, stdout=subprocess.PIPE, stderr=None if separate_stderr else subprocess.STDOUT,
                                   text=True, encoding="utf-8", errors="replace")
        try:
            assert process.stdout is not None
            for line in process.stdout:
                print(line, end="", flush=True)
                if log:
                    log.write(line)
            code = process.wait()
        except KeyboardInterrupt:
            if cancel_file is None:
                process.terminate()
                try: process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
                raise
            try:
                cancel_file.parent.mkdir(parents=True, exist_ok=True)
                cancel_file.touch()
                remaining, _ = process.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
                raise RuntimeError("Render did not acknowledge cancellation within 10 seconds; its process was stopped.") from None
            except BaseException:
                if process.poll() is None: process.kill(); process.wait()
                raise
            if remaining:
                print(remaining, end="", flush=True)
                if log: log.write(remaining)
            code = process.returncode
        except BaseException:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            raise
        finally:
            if process.stdout:
                process.stdout.close()
    except OSError as error:
        message = f"Could not start command: {error}\n"
        print(message, end="", file=sys.stderr)
        if log:
            log.write(message)
    finally:
        if log:
            log.close()
    return {
        "argv": argv,
        "cwd": str(root),
        "started_utc": started,
        "finished_utc": utc_now(),
        "elapsed_seconds": round(time.monotonic() - clock, 3),
        "returncode": code,
        "log": str(log_path.relative_to(root)) if log_path else None,
    }


def executable(root: Path, profile: str, name: str, require: bool = True) -> Path:
    base = root / ".local" / "build" / profile
    suffix = ".exe" if os.name == "nt" else ""
    candidates = [base / "bin" / (name + suffix), base / (name + suffix), base / "Release" / (name + suffix), base / "bin" / "Release" / (name + suffix)]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    if require:
        raise RuntimeError(f"{name} is not built. Run configure and build with --profile {profile} first.")
    return candidates[0]


def ci_commands(root: Path) -> list[tuple[str, list[str]]]:
    return [
        ("configure", ["cmake", "--preset", "headless"]),
        ("build", ["cmake", "--build", "--preset", "headless", "--config", "Release"]),
        ("ctest", ["ctest", "--preset", "headless", "--build-config", "Release", "--output-on-failure"]),
        ("python-tests", [sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py", "-v"]),
        ("render", [str(executable(root, "headless", "gtracker_cli", require=False)), "render", "examples/campfire.gtrk.json", "--output", ".local/demo.wav", "--json"]),
    ]


def run_ci(root: Path, only_if_due: bool = False) -> int:
    with ci_lock(root):
        status = ci_status(root)
        if only_if_due and not status["due"]:
            print("Local CI is not due; no full run started. " + status["qualification"])
            return 0
        run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
        directory = root / ".local" / "ci" / "runs" / run_id
        directory.mkdir(parents=True)
        report = {
            "schema": "gtracker.ci-run",
            "version": 1,
            "run_id": run_id,
            "status": "running",
            "started_utc": utc_now(),
            "finished_utc": None,
            "platform": {"system": platform.system(), "release": platform.release(), "machine": platform.machine(), "python": platform.python_version()},
            "local_binding_sha256": local_binding(root),
            "local_binding_basis": "Hostname, absolute checkout path, operating system, architecture; no machine ID lookup",
            "source": status["source"],
            "commands": [],
            "scope": status["scope"],
            "skipped_capabilities": ["Desktop compilation", "GUI interaction", "Listening assessment", "Microphone capture", "Other operating systems"],
        }
        report_path = directory / "report.json"

        def save() -> None:
            atomic_json(report_path, report)
            atomic_json(root / ".local" / "ci" / "latest.json", report)

        save()
        code = 1
        try:
            for index, (name, argv) in enumerate(ci_commands(root), start=1):
                command = run_command(root, argv, directory / f"{index:02d}-{name}.log")
                command["name"] = name
                report["commands"].append(command)
                save()
                if command["returncode"] != 0:
                    report["status"] = "failed"
                    break
            else:
                report["status"] = "passed"
                code = 0
        except KeyboardInterrupt:
            report["status"] = "interrupted"
            code = 130
        except Exception as error:
            report["status"] = "failed"
            report["error"] = str(error)
        finally:
            report["finished_utc"] = utc_now()
            try:
                report["source_after"] = source_state(root)
                if report["source_after"]["fingerprint_sha256"] != report["source"]["fingerprint_sha256"]:
                    report["source_changed_during_run"] = True
                    report["status"] = "source_changed"
                    code = 1
            except Exception as error:
                report["source_check_error"] = str(error)
                report["status"] = "failed"
                code = 1
            save()
        print(f"Local CI: {report['status']}. Evidence: {report_path.relative_to(root)}")
        return code


def doctor(root: Path) -> int:
    print(f"Repository: {root}")
    print(f"Platform: {platform.system()} {platform.release()} ({platform.machine()})")
    print(f"Python: {platform.python_version()} ({sys.executable})")
    missing = []
    for name in ("git", "cmake", "ctest", "ninja"):
        location = shutil.which(name)
        if not location:
            missing.append(name)
            print(f"{name}: MISSING")
            continue
        version = subprocess.run([location, "--version"], capture_output=True, text=True, errors="replace", check=False)
        line = (version.stdout or version.stderr).splitlines()
        print(f"{name}: {line[0] if line else location}")
    compiler = next((shutil.which(name) for name in ("c++", "clang++", "g++", "cl") if shutil.which(name)), None)
    print(f"C++ compiler in PATH: {compiler or 'not found; configure will diagnose the selected toolchain'}")
    print("Requirements: Python >= 3.10, CMake >= 3.25, Ninja, C++ compiler, and first-configure dependency access.")
    print("Desktop additionally needs the platform SDK/windowing development libraries. Audio capture and GUI are not tested by doctor.")
    if sys.version_info < (3, 10):
        missing.append("Python >= 3.10")
    return 1 if missing else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("doctor", help="Show build-tool availability without downloading anything.")
    for name in ("configure", "build", "test"):
        command = commands.add_parser(name)
        command.add_argument("--profile", choices=PROFILES, default="headless")
    render = commands.add_parser("render")
    render.add_argument("file", nargs="?", default="examples/campfire.gtrk.json")
    render.add_argument("--profile", choices=PROFILES, default="headless")
    render.add_argument("--output", default=".local/demo.wav")
    render.add_argument("--sample-rate", type=int)
    render.add_argument("--tail", type=float)
    render.add_argument("--script")
    render.add_argument("--production")
    render.add_argument("--bypass-production", action="store_true")
    render.add_argument("--stream", action="store_true")
    render.add_argument("--progress-json", action="store_true")
    render.add_argument("--cancel-file")
    render.add_argument("--start-frame", type=int)
    render.add_argument("--end-frame", type=int)
    render.add_argument("--block-frames", type=int)
    render.add_argument("--json", action="store_true")
    editor = commands.add_parser("editor")
    editor.add_argument("file", nargs="?")
    editor.add_argument("--take")
    editor.add_argument("--script")
    editor.add_argument("--smoke-test", action="store_true")
    editor.add_argument("--frames", type=int)
    editor.add_argument("--screenshot")
    editor.add_argument("--data-dir", default=".local")
    editor.add_argument("--resource-dir", default=str(ROOT))
    package = commands.add_parser("package", help="Build a local relocatable developer ZIP; does not publish a release.")
    package.add_argument("--profile", choices=PROFILES, default="desktop")
    package.add_argument("--output")
    importer = commands.add_parser("import-take", help="Preserve a PCM WAV and attach explicit source timing; no transcription.")
    importer.add_argument("input")
    importer.add_argument("--pattern", required=True)
    importer.add_argument("--output", required=True)
    importer.add_argument("--zero-frame", type=int, default=0)
    importer.add_argument("--input-offset-frames", type=int)
    importer.add_argument("--bpm", type=float)
    importer.add_argument("--selection-start-frame", type=int)
    importer.add_argument("--selection-end-frame", type=int)
    importer.add_argument("--job-id")
    inspector = commands.add_parser("take-info", help="Verify a preserved take and report sample/beat timing and analysis provenance.")
    inspector.add_argument("take")
    if __package__:
        from . import record as recorder
    else:
        import record as recorder
    recording = commands.add_parser("record-take", help="Explicit native capture, retained recovery and metadata-only take import.")
    recorder.configure_parser(recording)
    devices = commands.add_parser("input-devices", help="List current SDL recording-device selectors without starting capture.")
    devices.add_argument("--json", action="store_true")
    probe = commands.add_parser("calibration-probe", help="Write an original two-second broadband timing reference; no devices.")
    probe.add_argument("--output", required=True)
    probe.add_argument("--sample-rate", type=int)
    probe.add_argument("--seed", type=int)
    align = commands.add_parser("align-audio", help="Bounded relative file delay; no physical latency/clock claim or automatic offset.")
    align.add_argument("--reference", required=True)
    align.add_argument("--recorded", required=True)
    align.add_argument("--output")
    align.add_argument("--max-offset-ms", type=float)
    align.add_argument("--window-seconds", type=float)
    align.add_argument("--reference-channel", type=int, choices=(1, 2))
    align.add_argument("--recorded-channel", type=int, choices=(1, 2))
    for alignment_command in (probe, align):
        alignment_command.add_argument("--profile", choices=PROFILES, default="headless")
        alignment_command.add_argument("--json", action="store_true")
    for take_command in (importer, inspector):
        take_command.add_argument("--profile", choices=PROFILES, default="headless")
        take_command.add_argument("--json", action="store_true")
    status = commands.add_parser("ci-status")
    status.add_argument("--json", action="store_true")
    ci = commands.add_parser("ci")
    ci.add_argument("--if-due", action="store_true")
    args = parser.parse_args(argv)
    owned_cancel = None
    render_cancel = None
    try:
        if args.command == "doctor":
            return doctor(ROOT)
        if args.command == "ci-status":
            result = ci_status(ROOT)
            if args.json:
                print(json.dumps(result, indent=2))
            else:
                print("Local CI cadence: " + ("DUE" if result["due"] else "NOT DUE"))
                for reason in result["reasons"]:
                    print("- " + reason)
                print(result["qualification"])
                print(result["scope"])
            return 0
        if args.command == "ci":
            return run_ci(ROOT, args.if_due)
        if args.command == "record-take":
            return recorder.run_args(args)
        if args.command == "configure":
            command = ["cmake", "--preset", args.profile]
        elif args.command == "build":
            command = ["cmake", "--build", "--preset", args.profile, "--config", "Release"]
        elif args.command == "test":
            command = ["ctest", "--preset", args.profile, "--build-config", "Release", "--output-on-failure"]
        elif args.command == "render":
            command = [str(executable(ROOT, args.profile, "gtracker_cli")), "render", args.file, "--output", args.output]
            for option in ("sample_rate", "tail", "script", "production", "cancel_file", "start_frame", "end_frame", "block_frames"):
                if getattr(args, option) is not None:
                    command += ["--" + option.replace("_", "-"), str(getattr(args, option))]
            if args.bypass_production: command.append("--bypass-production")
            if args.stream: command.append("--stream")
            if args.progress_json: command.append("--progress-json")
            if args.stream or args.progress_json or any(getattr(args, name) is not None for name in ("cancel_file", "start_frame", "end_frame", "block_frames")):
                if args.cancel_file:
                    render_cancel = ROOT / args.cancel_file
                else:
                    owned_cancel = ROOT / ".local" / ("render-cancel-" + uuid.uuid4().hex)
                    render_cancel = owned_cancel
                    command += ["--cancel-file", str(render_cancel)]
            if args.json:
                command.append("--json")
        elif args.command == "package":
            output = args.output or f".local/packages/gtracker-{args.profile}-{platform.system()}-{platform.machine()}.zip"
            command = [sys.executable, str(ROOT / "tools/package.py"), "--profile", args.profile, "--output", output]
        elif args.command == "input-devices":
            command = [str(executable(ROOT, "desktop", "gtracker_capture")), "devices"]
            if args.json: command.append("--json")
        elif args.command in ("calibration-probe", "align-audio"):
            command = [str(executable(ROOT, args.profile, "gtracker_cli")), args.command]
            for option in ("output", "sample_rate", "seed", "reference", "recorded", "max_offset_ms", "window_seconds", "reference_channel", "recorded_channel"):
                value = getattr(args, option, None)
                if value is not None: command += ["--" + option.replace("_", "-"), str(value)]
            if args.json: command.append("--json")
        elif args.command in ("import-take", "take-info"):
            operation = "import" if args.command == "import-take" else "inspect"
            path = args.input if args.command == "import-take" else args.take
            command = [sys.executable, str(ROOT / "tools/takes.py"), operation, path, "--profile", args.profile]
            if args.command == "import-take":
                command += ["--pattern", args.pattern, "--output", args.output]
                for option in ("zero_frame", "input_offset_frames", "bpm", "selection_start_frame", "selection_end_frame", "job_id"):
                    if getattr(args, option) is not None:
                        command += ["--" + option.replace("_", "-"), str(getattr(args, option))]
            if args.json: command.append("--json")
        else:
            if args.take == "" or args.file == "":
                raise RuntimeError("Editor pattern/take paths cannot be empty.")
            if args.take and args.file:
                raise RuntimeError("Choose a pattern file or --take, not both.")
            command = [str(executable(ROOT, "desktop", "gtracker_editor"))]
            if not args.take: command.append(args.file or "examples/campfire.gtrk.json")
            if args.take: command += ["--take", args.take]
            for option in ("script", "frames", "screenshot", "data_dir", "resource_dir"):
                if getattr(args, option) is not None:
                    command += ["--" + option.replace("_", "-"), str(getattr(args, option))]
            if args.smoke_test:
                command.append("--smoke-test")
        return run_command(ROOT, command, echo=not (args.command in ("render", "import-take", "take-info", "input-devices", "calibration-probe", "align-audio") and args.json),
                           separate_stderr=args.command == "render" and args.progress_json, cancel_file=render_cancel)["returncode"]
    except (RuntimeError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    finally:
        if owned_cancel is not None:
            try: owned_cancel.unlink(missing_ok=True)
            except OSError as error: print(f"Could not remove owned render cancellation marker: {error}", file=sys.stderr)


if __name__ == "__main__":
    utf8_stdio()
    raise SystemExit(main())
