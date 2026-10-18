#!/usr/bin/env python3
"""Capture a short take, retain recovery audio, then attach metadata-only analysis."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import uuid
import wave

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from tools import dev, takes


def retain_report(recovery: Path, report: dict) -> None:
    try:
        dev.atomic_json(recovery / "record.json", report)
    except OSError:
        report["note"] = "Recovery files retained; controller receipt could not be written"


def verify_completed_capture(path: Path, report: dict, pattern: dict, job_id: str, pattern_hash: str) -> None:
    if (report.get("schema") != "gtracker.capture-result" or type(report.get("version")) is not int or report["version"] != 1
            or report.get("ok") is not True or report.get("status") != "complete"):
        raise ValueError("Native capture did not return a supported complete receipt")
    if (report.get("job_id") != job_id or type(report.get("source_pattern_revision")) is not int or report.get("source_pattern_revision") != pattern["revision"]
            or report.get("pattern_path") != "pattern.gtrk.json" or report.get("pattern_sha256") != pattern_hash
            or takes.file_hash(path / "pattern.gtrk.json") != pattern_hash):
        raise ValueError("Capture job/pattern snapshot identity changed")
    timing = report.get("timing")
    takes.exact_keys(timing, "sample_rate_hz channels tempo_bpm pattern_length_beats pre_roll_frames count_in_frames pattern_zero_frame musical_frames tail_frames planned_frames captured_frames input_offset_frames offset_status", "capture timing")
    rate = takes.integer(timing["sample_rate_hz"], 8000, 192000, "capture rate")
    channels = takes.integer(timing["channels"], 1, 2, "capture channels")
    takes.real(timing["tempo_bpm"], 20, 400, "capture tempo")
    takes.real(timing["pattern_length_beats"], 0, 256, "capture length")
    for key in ("pre_roll_frames", "count_in_frames", "pattern_zero_frame", "musical_frames", "tail_frames", "planned_frames", "captured_frames"):
        takes.integer(timing[key], 0, 32 * 1024 * 1024, key)
    if (timing["tempo_bpm"] != pattern["tempo_bpm"] or timing["pattern_length_beats"] != pattern["length_beats"]
            or timing["offset_status"] != "uncalibrated" or type(timing["input_offset_frames"]) is not int or timing["input_offset_frames"] != 0
            or timing["pattern_zero_frame"] != timing["pre_roll_frames"] + timing["count_in_frames"]
            or timing["planned_frames"] != timing["pattern_zero_frame"] + timing["musical_frames"] + timing["tail_frames"]
            or timing["captured_frames"] != timing["planned_frames"] or not timing["musical_frames"]
            or timing["planned_frames"] > rate * 120 or timing["planned_frames"] * channels > 32 * 1024 * 1024):
        raise ValueError("Capture sample/beat plan is inconsistent or claims unsupported calibration")
    if timing["musical_frames"] != math.ceil(pattern["length_beats"] * 60.0 / pattern["tempo_bpm"] * rate):
        raise ValueError("Capture musical frame span disagrees with its pattern")
    source = report.get("source")
    takes.exact_keys(source, "path sha256 sample_rate_hz channels sample_width_bits frames size_bytes", "captured source")
    for key in ("sample_rate_hz", "channels", "sample_width_bits", "frames", "size_bytes"):
        takes.integer(source[key], 1, takes.MAX_INTEGER, "capture source " + key)
    if (source["path"] != "source.wav" or source["sample_rate_hz"] != rate or source["channels"] != channels
            or source["sample_width_bits"] != 16 or source["frames"] != timing["captured_frames"]
            or takes.file_hash(path / "source.wav") != source["sha256"]
            or (path / "source.wav").stat().st_size != source["size_bytes"]):
        raise ValueError("Captured source no longer matches its receipt")
    with wave.open(str(path / "source.wav"), "rb") as audio:
        if (audio.getframerate(), audio.getnchannels(), audio.getsampwidth(), audio.getnframes()) != (rate, channels, 2, source["frames"]):
            raise ValueError("Captured WAV format disagrees with its receipt")
    if report.get("reference_path") != "reference.wav" or takes.file_hash(path / "reference.wav") != report.get("reference_sha256"):
        raise ValueError("Count-in reference is missing or changed")


def record_take(pattern_path: Path, output: Path, *, device: str | None = None, simulate: Path | None = None,
                sample_rate: int | None = None, channels: int | None = None, count_in: int = 4,
                pre_roll: float = 1, tail: float = 2, metronome: bool = False, silent: bool = False,
                calibration_probe: bool = False,
                realtime_simulation: bool = False, simulate_disconnect_frame: int | None = None,
                simulate_cancel_frame: int | None = None, cancel_file: Path | None = None,
                job_id: str | None = None) -> dict:
    if (device is None) == (simulate is None) or device == "":
        raise ValueError("Choose exactly one explicit --device or --simulate input")
    if simulate is None and (realtime_simulation or simulate_disconnect_frame is not None or simulate_cancel_frame is not None):
        raise ValueError("Simulation flags require --simulate")
    identity = "take-" + uuid.uuid4().hex if job_id is None else job_id
    if not isinstance(identity, str) or not 1 <= len(identity.encode("utf-8")) <= 128:
        raise ValueError("Job ID must contain 1..128 UTF-8 bytes")
    output = output.resolve()
    local = (ROOT / ".local").resolve()
    if not output.is_relative_to(local) or output == local:
        raise ValueError("Recorded take bundles must be written under ignored .local/.")
    if output.exists():
        raise ValueError("Take output already exists; choose a new bundle directory")
    executable = dev.executable(ROOT, "desktop", "gtracker_capture")
    output.parent.mkdir(parents=True, exist_ok=True)
    recovery = output.with_name(output.name + ".recovery-" + uuid.uuid4().hex[:12])
    recovery.mkdir() # Exclusive ownership; retain this directory on any failure.
    raw = recovery / "raw"
    owned_cancel = recovery / "cancel"
    report = {"ok": False, "command": "record-take", "job_id": identity, "take": None,
              "recovery_path": str(recovery), "capture_status": "not_started", "analysis_status": "not_started", "exit_code": 1}
    try:
        snapshot = recovery / "input.gtrk.json"
        shutil.copyfile(pattern_path, snapshot)
        pattern, _ = takes.validate_pattern(snapshot, "desktop")
        pattern_hash = takes.file_hash(snapshot)
        command = [str(executable), "record", "--pattern", str(snapshot), "--output", str(raw), "--job-id", identity,
                   "--count-in", str(count_in), "--pre-roll", str(pre_roll), "--tail", str(tail), "--cancel-file", str(owned_cancel), "--json"]
        if device is not None:
            command += ["--device", device]
        else:
            command += ["--simulate", str(simulate.resolve())]
        for name, value in (("sample-rate", sample_rate), ("channels", channels), ("simulate-disconnect-frame", simulate_disconnect_frame), ("simulate-cancel-frame", simulate_cancel_frame)):
            if value is not None: command += ["--" + name, str(value)]
        for name, value in (("metronome", metronome), ("silent", silent), ("calibration-probe", calibration_probe), ("realtime-simulation", realtime_simulation)):
            if value: command.append("--" + name)
        process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8")
        interrupted = False
        try:
            # Periodic communicate timeouts allow an external cancel file without
            # a second thread. Native servicing has its own duration bound.
            import time
            deadline = time.monotonic() + 150
            while True:
                if cancel_file is not None and cancel_file.exists(): owned_cancel.touch()
                try:
                    stdout, stderr = process.communicate(timeout=0.1)
                    break
                except subprocess.TimeoutExpired:
                    if time.monotonic() > deadline: raise TimeoutError("Native capture exceeded its 150-second controller deadline")
        except KeyboardInterrupt:
            interrupted = True
            owned_cancel.touch()
            stdout, stderr = process.communicate(timeout=15)
        finally:
            if process.poll() is None:
                owned_cancel.touch()
                try:
                    process.communicate(timeout=10)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    try: process.communicate(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.communicate()
        (recovery / "native-stdout.json").write_text(stdout, encoding="utf-8")
        (recovery / "native-stderr.txt").write_text(stderr, encoding="utf-8")
        capture = takes.read_json(raw / "capture.json") if (raw / "capture.json").is_file() else json.loads(stdout)
        if type(capture) is not dict: raise ValueError("Native capture receipt must be a JSON object")
        report["capture_status"] = capture.get("status", "failed")
        if interrupted or process.returncode == 130:
            report.update(exit_code=130, error="Capture cancelled; received source and context retained for recovery")
            retain_report(recovery, report)
            return report
        if process.returncode != 0:
            report["error"] = capture.get("error") or "Native capture failed; inspect retained recovery files"
            retain_report(recovery, report)
            return report
        if json.loads(stdout) != capture:
            raise ValueError("Native stdout does not match its persisted capture receipt")
        verify_completed_capture(raw, capture, pattern, identity, pattern_hash)
        rate = capture["timing"]["sample_rate_hz"]
        expected = {"pre_roll_frames": math.floor(pre_roll * rate + 0.5),
                    "count_in_frames": math.floor(count_in * 60.0 / pattern["tempo_bpm"] * rate + 0.5),
                    "tail_frames": math.floor(tail * rate + 0.5)}
        if any(capture["timing"][key] != value for key, value in expected.items()):
            raise ValueError("Capture timing does not match the requested context/count-in/tail")
        if (simulate is not None and capture.get("source_kind") != "file_simulation") or (simulate is None and capture.get("source_kind") not in ("sdl_recording", "sdl_simulated_driver")):
            raise ValueError("Capture receipt does not describe the selected input kind")
        report["analysis_status"] = "running"
        bundle = recovery / "bundle"
        take = takes.import_take(raw / "source.wav", raw / "pattern.gtrk.json", bundle,
                                 zero_frame=capture["timing"]["pattern_zero_frame"], job_id=identity, profile="desktop")
        if any(take["source"][key] != capture["source"][key] for key in ("sha256", "sample_rate_hz", "channels", "frames", "sample_width_bits", "size_bytes")):
            raise ValueError("Imported source differs from its capture receipt")
        shutil.copyfile(raw / "capture.json", bundle / "capture.json")
        shutil.copyfile(raw / "reference.wav", bundle / "reference.wav")
        verify_completed_capture(bundle, capture, pattern, identity, pattern_hash)
        takes.inspect_take(bundle / "take.json", profile="desktop")
        if output.exists(): raise ValueError("Take output appeared during recording; recovery retained")
        bundle.rename(output)
        report.update(ok=True, take=str(output / "take.json"), analysis_status="metadata_only", exit_code=0)
        if not recovery.resolve().is_relative_to(local): raise RuntimeError("Refusing capture cleanup outside .local/.")
        try:
            shutil.rmtree(recovery)
            report["recovery_path"] = None
        except OSError:
            report["note"] = "Take is complete; redundant recovery files could not be removed"
        return report
    except KeyboardInterrupt:
        if report["analysis_status"] == "running": report["analysis_status"] = "cancelled"
        report.update(exit_code=130, error="Take operation cancelled; capture recovery retained")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, wave.Error, EOFError) as error:
        if report["analysis_status"] == "running": report["analysis_status"] = "failed"
        report["error"] = str(error)
    retain_report(recovery, report)
    return report


def configure_parser(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--pattern", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--device")
    source.add_argument("--simulate", type=Path)
    parser.add_argument("--sample-rate", type=int)
    parser.add_argument("--channels", type=int, choices=(1, 2))
    parser.add_argument("--count-in", type=int, default=4)
    parser.add_argument("--pre-roll", type=float, default=1)
    parser.add_argument("--tail", type=float, default=2)
    parser.add_argument("--metronome", action="store_true")
    parser.add_argument("--calibration-probe", action="store_true")
    parser.add_argument("--silent", action="store_true")
    parser.add_argument("--realtime-simulation", action="store_true")
    parser.add_argument("--simulate-disconnect-frame", type=int)
    parser.add_argument("--simulate-cancel-frame", type=int)
    parser.add_argument("--cancel-file", type=Path)
    parser.add_argument("--job-id")
    parser.add_argument("--json", action="store_true")


def run_args(args: argparse.Namespace) -> int:
    arguments = vars(args).copy()
    arguments.pop("json")
    arguments.pop("command", None)
    pattern = arguments.pop("pattern")
    output = arguments.pop("output")
    try:
        report = record_take(pattern, output, **arguments)
    except KeyboardInterrupt:
        report = {"ok": False, "error": "Take operation cancelled before capture started", "exit_code": 130}
    except (OSError, ValueError, RuntimeError) as error:
        report = {"ok": False, "error": str(error), "exit_code": 1}
    if args.json:
        print(json.dumps(report, indent=2, allow_nan=False))
    elif report["ok"]:
        print(f"Take saved: {report['take']} (metadata_only; no transcript)")
    else:
        print(f"record: {report['error']}; recovery: {report.get('recovery_path')}", file=sys.stderr)
    return report["exit_code"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    configure_parser(parser)
    return run_args(parser.parse_args(argv))


if __name__ == "__main__":
    dev.utf8_stdio()
    raise SystemExit(main())
