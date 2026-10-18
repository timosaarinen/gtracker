#!/usr/bin/env python3
"""Metadata-only analysis worker. It does not transcribe notes or techniques."""

from __future__ import annotations

import argparse
from array import array
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import sys
import uuid
import wave


ENGINE_VERSION = "0.1.0"


def decode_pcm(data: bytes, width: int):
    if len(data) % width:
        raise ValueError("Truncated PCM sample in input WAV")
    if width == 1:
        return ((sample - 128) / 128.0 for sample in data)
    if width in (2, 4):
        values = array("h" if width == 2 else "i")
        values.frombytes(data)
        if sys.byteorder != "little":
            values.byteswap()
        scale = float(1 << (width * 8 - 1))
        return (sample / scale for sample in values)
    if width == 3:
        return (int.from_bytes(data[index:index + 3], "little", signed=True) / 8388608.0 for index in range(0, len(data), 3))
    raise ValueError("Only uncompressed 8/16/24/32-bit integer PCM WAV is supported")


def analyze_file(path: Path, job_id: str, pattern_revision: int, bpm: float) -> dict:
    if not job_id or len(job_id) > 128:
        raise ValueError("Job ID must contain 1 to 128 characters")
    if pattern_revision < 0:
        raise ValueError("Pattern revision cannot be negative")
    if not math.isfinite(bpm) or bpm <= 0:
        raise ValueError("BPM must be finite and positive")
    before = path.stat()
    with wave.open(str(path), "rb") as source:
        channels = source.getnchannels()
        width = source.getsampwidth()
        rate = source.getframerate()
        declared_frames = source.getnframes()
        if source.getcomptype() != "NONE" or width not in (1, 2, 3, 4):
            raise ValueError("Only uncompressed 8/16/24/32-bit integer PCM WAV is supported")
        if channels not in (1, 2):
            raise ValueError("This baseline accepts mono or stereo WAV only")
        if rate <= 0 or declared_frames <= 0:
            raise ValueError("Input WAV must contain audio frames and a positive sample rate")
        peaks = [0.0] * channels
        squares = [0.0] * channels
        frames = 0
        while True:
            data = source.readframes(65536)
            if not data:
                break
            if len(data) % (width * channels):
                raise ValueError("Truncated PCM frame in input WAV")
            frames += len(data) // (width * channels)
            for index, sample in enumerate(decode_pcm(data, width)):
                channel = index % channels
                peaks[channel] = max(peaks[channel], abs(sample))
                squares[channel] += sample * sample
        if frames != declared_frames:
            raise ValueError("Input WAV is truncated: frame count differs from its header")
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError("Input WAV changed while analysis was running; submit a stable recording")
    return {
        "schema": "gtracker.analysis-result",
        "version": 1,
        "job_id": job_id,
        "source_pattern_revision": pattern_revision,
        "status": "metadata_only",
        "capabilities": {"pcm_metadata": True, "peak_rms": True, "note_transcription": False, "technique_recognition": False},
        "source": {
            "name": path.name,
            "sha256": digest.hexdigest(),
            "size_bytes": after.st_size,
            "encoding": "integer_pcm",
            "sample_width_bits": width * 8,
            "channels": channels,
            "sample_rate_hz": rate,
            "frames": frames,
            "duration_seconds": frames / rate,
        },
        "timing": {"coordinate_system": "source_samples", "selection_start_frame": 0, "selection_end_frame": frames, "bpm_hint": bpm},
        "diagnostics": {
            "peak": max(peaks),
            "rms": math.sqrt(sum(squares) / (frames * channels)),
            "channels": [{"index": index, "peak": peaks[index], "rms": math.sqrt(squares[index] / frames)} for index in range(channels)],
        },
        "events": [],
        "limitations": ["No notes, onsets, string/fret assignments, articulations, or body hits are inferred. Empty events means transcription is unavailable, not that the recording contains no music."],
        "provenance": {"engine": "gtracker.metadata-baseline", "engine_version": ENGINE_VERSION, "python": platform.python_version(), "created_utc": datetime.now(timezone.utc).isoformat(timespec="seconds")},
    }


def write_result(path: Path, result: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        temporary.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--job-id", required=True)
    parser.add_argument("--pattern-revision", type=int, required=True)
    parser.add_argument("--bpm", type=float, required=True)
    args = parser.parse_args(argv)
    try:
        if args.input.resolve() == args.output.resolve():
            raise ValueError("Output must differ from input; source audio is preserved")
        result = analyze_file(args.input, args.job_id, args.pattern_revision, args.bpm)
        write_result(args.output, result)
    except (OSError, ValueError, wave.Error, EOFError) as error:
        print(f"analysis error: {error}", file=sys.stderr)
        return 1
    print(f"metadata_only: {result['source']['frames']} frames analyzed; no transcript generated. Result: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
