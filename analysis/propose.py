#!/usr/bin/env python3
"""Opt-in single-note proposals from one selected take channel; no automatic edits."""
from __future__ import annotations

import argparse
from array import array
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import uuid
import wave

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path: sys.path.insert(0, str(ROOT))
from analysis import analyze, monophonic
from tools import dev, takes


def snapshot(path: Path) -> bytes:
    with path.open("rb") as source: data = source.read(1024*1024+1)
    if len(data) > 1024*1024: raise ValueError("Take/pattern JSON exceeds 1 MiB")
    return data


def propose_take(path: Path, *, channel: int, job_id: str | None = None, profile: str = "headless") -> dict:
    take_bytes = snapshot(path)
    take = json.loads(take_bytes)
    takes.validate_metadata(take,path)
    takes.inspect_take(path, profile=profile)
    source_path = takes.bundle_file(path, take["source"]["path"])
    pattern_path = takes.bundle_file(path, take["pattern_path"])
    if source_path.stat().st_size > 256*1024*1024: raise ValueError("Source WAV exceeds the native preview's 256 MiB limit")
    pattern_bytes = snapshot(pattern_path)
    pattern, native = takes.validate_pattern(pattern_path, profile)
    channel = takes.integer(channel, 1, take["source"]["channels"], "channel")
    identity = "proposal-" + uuid.uuid4().hex if job_id is None else job_id
    if not isinstance(identity, str) or not 1 <= len(identity.encode("utf-8")) <= 128: raise ValueError("Job ID must contain 1..128 UTF-8 bytes")
    alignment = take["alignment"]
    start, end = alignment["selection_start_frame"], alignment["selection_end_frame"]
    if end-start > take["source"]["sample_rate_hz"]*30: raise ValueError("Choose a take selection of at most 30 seconds")
    with wave.open(str(source_path), "rb") as source:
        if (source.getnchannels(),source.getframerate(),source.getsampwidth()*8,source.getnframes()) != (take["source"]["channels"],take["source"]["sample_rate_hz"],take["source"]["sample_width_bits"],take["source"]["frames"]):
            raise ValueError("Source WAV format changed during proposals")
        source.setpos(start)
        raw = source.readframes(end-start)
    samples = array("d", (sample for index,sample in enumerate(analyze.decode_pcm(raw,take["source"]["sample_width_bits"]//8))
                          if index % take["source"]["channels"] == channel-1))
    if len(samples) != end-start: raise ValueError("Source selection changed or was truncated")
    notes, parameters = monophonic.propose(samples, take["source"]["sample_rate_hz"], start, pattern["instrument"]["tuning_midi"])
    if takes.file_hash(source_path) != take["source"]["sha256"] or snapshot(pattern_path) != pattern_bytes or snapshot(path) != take_bytes:
        raise ValueError("Source, pattern or take timing changed during proposals; repeat the job")
    engine_files = [Path(monophonic.__file__), Path(__file__), Path(analyze.__file__), Path(takes.__file__)]
    code_hash = hashlib.sha256(b"\0".join(file.read_bytes() for file in engine_files)).hexdigest()
    return {"schema": "gtracker.note-proposals", "version": 1, "job_id": identity, "take_id": take["id"],
            "source_pattern_revision": pattern["revision"], "source_pattern": pattern,
            "source": {key: take["source"][key] for key in ("sha256", "frames", "sample_rate_hz", "channels")} | {"channel": channel},
            "alignment": alignment, "status": "proposals" if notes else "no_confident_candidates", "parameters": parameters, "notes": notes,
            "limitations": ["Explicit single-note assumption; chords, overlapping strings and octave errors are not resolved",
                            "No slides, bends, hammer/pull-off, damping, harmonics, body-hit or technique recognition",
                            "Periodicity and voiced fraction are diagnostics, not calibrated probabilities",
                            "String/fret alternatives are pitch-compatible possibilities, not detected fingering",
                            "End frames delimit stable pitch regions, not detected physical release; acceptance adds plucks only",
                            "Onset timing is coarse acoustic timing with uncertainty; no latency calibration or grid quantization",
                            "No real-guitar qualification; box decimation can alias and note attacks/noise can fail"],
            "provenance": {"engine": monophonic.ENGINE, "engine_version": monophonic.VERSION,
                           "source_code_sha256": code_hash, "python": platform.python_version(), "created_utc": dev.utc_now(),
                           "renderer_version": native["renderer_version"], "renderer_build_id": native["renderer_build_id"]}}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--take", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--channel", type=int, required=True, help="Explicit one-based source channel; no automatic downmix")
    parser.add_argument("--assume-monophonic", action="store_true", required=True, help="Acknowledge the single-note research assumption")
    parser.add_argument("--job-id")
    parser.add_argument("--profile", choices=dev.PROFILES, default="headless")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        output = args.output.resolve()
        if not output.is_relative_to((ROOT / ".local").resolve()): raise ValueError("Proposal output must stay under ignored .local/")
        if output.exists(): raise ValueError("Proposal output already exists; use a new file to preserve previous jobs")
        result = propose_take(args.take, channel=args.channel, job_id=args.job_id, profile=args.profile)
        if output.exists(): raise ValueError("Proposal output appeared during analysis")
        dev.atomic_json(output, result)
        report = {"ok": True, "status": result["status"], "notes": len(result["notes"]), "output": str(output), "job_id": result["job_id"]}
        print(json.dumps(report) if args.json else f"{report['status']}: {report['notes']} candidates; review {output}")
        return 0
    except (OSError, ValueError, RuntimeError, wave.Error, EOFError, subprocess.SubprocessError) as error:
        print(f"proposal error: {error}", file=sys.stderr); return 1


if __name__ == "__main__":
    dev.utf8_stdio()
    raise SystemExit(main())
