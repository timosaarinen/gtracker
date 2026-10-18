#!/usr/bin/env python3
"""Import preserved PCM takes and inspect their explicit source-sample timing."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import uuid
import wave

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from analysis import analyze
from tools import dev

MAX_INTEGER = 9007199254740991


def exact_keys(value: dict, names: str, where: str) -> None:
    if type(value) is not dict or set(value) != set(names.split()):
        raise ValueError(f"{where} has missing or unknown fields")


def integer(value, minimum: int, maximum: int, where: str) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{where} must be an integer in {minimum}..{maximum}")
    return value


def real(value, minimum: float, maximum: float, where: str) -> float:
    if type(value) not in (int, float) or not math.isfinite(value) or not minimum <= value <= maximum:
        raise ValueError(f"{where} must be finite and in {minimum}..{maximum}")
    return float(value)


def read_json(path: Path) -> dict:
    if path.stat().st_size > 1024 * 1024:
        raise ValueError("Take/job JSON exceeds the 1 MiB authoring limit")
    return json.loads(path.read_text(encoding="utf-8"))


def bundle_file(take_path: Path, reference: str) -> Path:
    if not isinstance(reference, str) or not reference or "\\" in reference or ":" in reference:
        raise ValueError("Take references must be relative portable paths")
    path = PurePosixPath(reference)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("Take reference escapes its bundle")
    directory = take_path.parent.resolve()
    result = (directory / path).resolve()
    if not result.is_relative_to(directory):
        raise ValueError("Take reference escapes its bundle")
    return result


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_frame_to_beat(frame: float, take: dict) -> float:
    timing = take["alignment"]
    return ((frame - timing["pattern_zero_frame"] - timing["input_offset_frames"])
            / take["source"]["sample_rate_hz"] * timing["tempo_bpm"] / 60.0)


def validate_metadata(take: dict, path: Path) -> None:
    exact_keys(take, "schema version id origin pattern_path source_pattern_revision source alignment analysis_path created_utc", "take")
    if take["schema"] != "gtracker.take" or type(take["version"]) is not int or take["version"] != 1:
        raise ValueError("Unsupported take schema/version; expected gtracker.take v1")
    if take["origin"] != "import":
        raise ValueError("Unsupported take origin; only preserved imports are implemented")
    if not isinstance(take["id"], str) or not 1 <= len(take["id"].encode("utf-8")) <= 128:
        raise ValueError("Take ID must contain 1..128 UTF-8 bytes")
    integer(take["source_pattern_revision"], 1, MAX_INTEGER, "source_pattern_revision")
    for field in ("pattern_path", "analysis_path"):
        bundle_file(path, take[field])
    source = take["source"]
    exact_keys(source, "path name sha256 size_bytes encoding sample_width_bits channels sample_rate_hz frames duration_seconds", "source")
    bundle_file(path, source["path"])
    if source["encoding"] != "integer_pcm" or source["sample_width_bits"] not in (8, 16, 24, 32):
        raise ValueError("Take source must be 8/16/24/32-bit integer PCM")
    integer(source["sample_width_bits"], 8, 32, "sample_width_bits")
    integer(source["channels"], 1, 2, "channels")
    integer(source["sample_rate_hz"], 8000, 192000, "sample_rate_hz")
    integer(source["frames"], 1, MAX_INTEGER, "frames")
    integer(source["size_bytes"], 1, MAX_INTEGER, "size_bytes")
    if not isinstance(source["name"], str) or not 1 <= len(source["name"].encode("utf-8")) <= 1023:
        raise ValueError("Source name must contain 1..1023 UTF-8 bytes")
    if not isinstance(source["sha256"], str) or len(source["sha256"]) != 64 or any(c not in "0123456789abcdef" for c in source["sha256"]):
        raise ValueError("Source SHA-256 must be 64 lowercase hexadecimal characters")
    duration = real(source["duration_seconds"], 0, MAX_INTEGER, "duration_seconds")
    if not math.isclose(duration, source["frames"] / source["sample_rate_hz"], rel_tol=1e-12):
        raise ValueError("Source duration disagrees with its frame count/sample rate")
    timing = take["alignment"]
    exact_keys(timing, "coordinate_system pattern_zero_frame input_offset_frames offset_status tempo_bpm pattern_length_beats selection_start_frame selection_end_frame", "alignment")
    if timing["coordinate_system"] != "source_samples":
        raise ValueError("Take timing must use source_samples")
    integer(timing["pattern_zero_frame"], 0, source["frames"], "pattern_zero_frame")
    integer(timing["input_offset_frames"], -source["frames"], source["frames"], "input_offset_frames")
    if timing["offset_status"] not in ("uncalibrated", "manual"):
        raise ValueError("Only uncalibrated/manual input offsets are implemented")
    if timing["offset_status"] == "uncalibrated" and timing["input_offset_frames"] != 0:
        raise ValueError("A nonzero input offset must be explicitly manual")
    real(timing["tempo_bpm"], 20, 400, "tempo_bpm")
    if real(timing["pattern_length_beats"], 0, 256, "pattern_length_beats") == 0:
        raise ValueError("pattern_length_beats must be positive")
    start = integer(timing["selection_start_frame"], 0, source["frames"], "selection_start_frame")
    integer(timing["selection_end_frame"], start + 1, source["frames"], "selection_end_frame")
    if not isinstance(take["created_utc"], str) or not take["created_utc"]:
        raise ValueError("created_utc is required")


def validate_pattern(path: Path, profile: str) -> tuple[dict, dict]:
    result = subprocess.run([str(dev.executable(ROOT, profile, "gtracker_cli")), "validate", str(path), "--json"],
                            cwd=ROOT, capture_output=True, encoding="utf-8", timeout=30, check=False)
    if result.returncode:
        raise ValueError("Native pattern validation failed: " + (result.stdout or result.stderr).strip())
    return read_json(path), json.loads(result.stdout)


def import_take(input_path: Path, pattern_path: Path, output: Path, *, zero_frame: int = 0,
                input_offset: int | None = None, bpm: float | None = None,
                selection_start: int = 0, selection_end: int | None = None,
                job_id: str | None = None, profile: str = "headless") -> dict:
    output = output.resolve()
    local = (ROOT / ".local").resolve()
    if not output.is_relative_to(local) or output == local:
        raise ValueError("Imported take bundles must be written under ignored .local/.")
    if output.exists():
        raise ValueError("Take output already exists; choose a new bundle directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = output.with_name("." + output.name + ".import-" + uuid.uuid4().hex)
    stage.mkdir()
    try:
        shutil.copyfile(pattern_path, stage / "pattern.gtrk.json")
        pattern, _ = validate_pattern(stage / "pattern.gtrk.json", profile)
        tempo = pattern["tempo_bpm"] if bpm is None else real(bpm, 20, 400, "bpm")
        identity = ("take-" + uuid.uuid4().hex) if job_id is None else job_id
        result = analyze.analyze_file(input_path, identity, pattern["revision"], tempo)
        shutil.copyfile(input_path, stage / "source.wav")
        if file_hash(stage / "source.wav") != result["source"]["sha256"]:
            raise ValueError("Source WAV changed while being copied; no take was published")
        take = {
            "schema": "gtracker.take", "version": 1, "id": identity, "origin": "import",
            "pattern_path": "pattern.gtrk.json", "source_pattern_revision": pattern["revision"],
            "source": {"path": "source.wav", **result["source"]},
            "alignment": {"coordinate_system": "source_samples", "pattern_zero_frame": zero_frame,
                          "input_offset_frames": 0 if input_offset is None else input_offset,
                          "offset_status": "uncalibrated" if input_offset is None else "manual",
                          "tempo_bpm": tempo, "pattern_length_beats": pattern["length_beats"],
                          "selection_start_frame": selection_start,
                          "selection_end_frame": result["source"]["frames"] if selection_end is None else selection_end},
            "analysis_path": "analysis.json", "created_utc": dev.utc_now(),
        }
        validate_metadata(take, stage / "take.json")
        dev.atomic_json(stage / "analysis.json", result)
        dev.atomic_json(stage / "take.json", take)
        if output.exists():
            raise ValueError("Take output appeared during import; choose a new bundle directory")
        stage.rename(output)
        return take
    finally:
        # This function created stage exclusively. Verify the absolute target
        # before recursive cleanup; never delete the requested/user-owned output.
        if stage.exists():
            if not stage.resolve().is_relative_to(local):
                raise RuntimeError("Refusing to clean an import stage outside .local/.")
            shutil.rmtree(stage)


def inspect_take(path: Path, *, profile: str = "headless") -> dict:
    take = read_json(path)
    validate_metadata(take, path)
    pattern, _ = validate_pattern(bundle_file(path, take["pattern_path"]), profile)
    result = read_json(bundle_file(path, take["analysis_path"]))
    if (type(result) is not dict or type(result.get("source")) is not dict or
            result.get("schema") != "gtracker.analysis-result" or type(result.get("version")) is not int or result["version"] != 1 or
            type(result.get("source_pattern_revision")) is not int or
            result.get("job_id") != take["id"] or result.get("source_pattern_revision") != take["source_pattern_revision"] or
            result.get("source", {}).get("sha256") != take["source"]["sha256"]):
        raise ValueError("Analysis job/source/revision does not match this take")
    if result.get("status") != "metadata_only" or result.get("events") != []:
        raise ValueError("Only metadata-only analysis is implemented; no transcript may be imported")
    capabilities = result.get("capabilities", {})
    exact_keys(capabilities, "pcm_metadata peak_rms note_transcription technique_recognition", "analysis capabilities")
    if capabilities.get("note_transcription") is not False or capabilities.get("technique_recognition") is not False:
        raise ValueError("Analysis capabilities are inconsistent with metadata-only status")
    actual = analyze.analyze_file(bundle_file(path, take["source"]["path"]), take["id"],
                                  take["source_pattern_revision"], take["alignment"]["tempo_bpm"])
    for key in ("sha256", "size_bytes", "frames", "channels", "sample_rate_hz", "sample_width_bits", "encoding"):
        if actual["source"][key] != take["source"][key] or result["source"].get(key) != take["source"][key]:
            raise ValueError("Preserved source no longer matches take/analysis metadata: " + key)
    alignment = take["alignment"]
    return {"ok": True, "command": "take-info", "take": str(path), "take_id": take["id"],
            "source": take["source"], "alignment": alignment,
            "selection_start_beat": source_frame_to_beat(alignment["selection_start_frame"], take),
            "selection_end_beat": source_frame_to_beat(alignment["selection_end_frame"], take),
            "pattern_revision": pattern["revision"], "analysis_status": result["status"],
            "analysis_matches_pattern_revision": pattern["revision"] == take["source_pattern_revision"],
            "capabilities": result["capabilities"], "diagnostics": actual["diagnostics"]}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    importer = subcommands.add_parser("import")
    importer.add_argument("input", type=Path)
    importer.add_argument("--pattern", type=Path, required=True)
    importer.add_argument("--output", type=Path, required=True)
    importer.add_argument("--zero-frame", type=int, default=0)
    importer.add_argument("--input-offset-frames", type=int)
    importer.add_argument("--bpm", type=float)
    importer.add_argument("--selection-start-frame", type=int, default=0)
    importer.add_argument("--selection-end-frame", type=int)
    importer.add_argument("--job-id")
    inspector = subcommands.add_parser("inspect")
    inspector.add_argument("take", type=Path)
    for command in (importer, inspector):
        command.add_argument("--profile", choices=dev.PROFILES, default="headless")
        command.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.command == "import":
            take = import_take(args.input, args.pattern, args.output, zero_frame=args.zero_frame,
                               input_offset=args.input_offset_frames, bpm=args.bpm,
                               selection_start=args.selection_start_frame, selection_end=args.selection_end_frame,
                               job_id=args.job_id, profile=args.profile)
            report = {"ok": True, "command": "import-take", "take": str(args.output / "take.json"),
                      "take_id": take["id"], "analysis_status": "metadata_only", "source": take["source"]}
        else:
            report = inspect_take(args.take, profile=args.profile)
        if args.json:
            print(json.dumps(report, indent=2, allow_nan=False))
        else:
            print(f"metadata_only: {report['take']}; original source preserved, no transcript")
        return 0
    except KeyboardInterrupt:
        report = {"ok": False, "error": "Take operation cancelled"}
        code = 130
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, wave.Error, EOFError) as error:
        report = {"ok": False, "error": str(error)}
        code = 1
    if args.json:
        print(json.dumps(report, allow_nan=False))
    else:
        print("take: " + report["error"], file=sys.stderr)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
