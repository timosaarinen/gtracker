#!/usr/bin/env python3
"""Known-score pitch/decay probes through the native renderer; no transcription."""
from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import platform
import subprocess
import sys
import time
import wave
import uuid

if __package__:
    from . import dev
else:
    import dev

MODELS = ("baseline_0_1", "contact_0_2", "retained_0_5")


def known_pitch(samples: list[float], rate: int, expected_hz: float) -> dict:
    """Conditioned autocorrelation near a declared pitch, not note recognition."""
    if rate <= 0 or not math.isfinite(expected_hz) or expected_hz <= 0:
        raise ValueError("Pitch probe requires a positive rate and declared frequency")
    first = max(2, math.floor(rate / (expected_hz * 1.1)))
    last = math.ceil(rate / (expected_hz * 0.9))
    if len(samples) < last * 3: return {"status": "insufficient_samples", "hz": None}
    mean = sum(samples) / len(samples)
    signal = [value - mean for value in samples]
    if sum(value*value for value in signal) / len(signal) < 1e-12:
        return {"status": "no_signal", "hz": None}
    correlations = []
    for lag in range(first, last + 1):
        count = len(signal) - lag
        product = aa = bb = 0.0
        for i in range(count):
            a, b = signal[i], signal[i + lag]
            product += a*b; aa += a*a; bb += b*b
        correlations.append(product / math.sqrt(aa*bb) if aa*bb > 0 else 0)
    peak = max(range(len(correlations)), key=correlations.__getitem__)
    lag = float(first + peak)
    if 0 < peak < len(correlations)-1:
        a, b, c = correlations[peak-1:peak+2]
        denominator = a - 2*b + c
        if denominator < 0: lag += max(-0.5, min(0.5, 0.5 * (a-c) / denominator))
    hz = rate / lag
    reliable = correlations[peak] >= 0.6 and 0 < peak < len(correlations)-1
    return {"status": "estimated" if reliable else "unreliable", "hz": hz if reliable else None,
            "candidate_hz": hz, "error_cents": 1200*math.log2(hz/expected_hz) if reliable else None,
            "normalized_correlation": correlations[peak], "search_prior": "declared expected pitch +/-10%; excludes octave alternatives"}


def decay_20db(samples: list[float], rate: int) -> dict:
    """First three 50ms RMS windows below 1/10 of the 200..250ms reference."""
    if rate <= 0: raise ValueError("Decay probe requires a positive rate")
    width = max(1, round(rate * 0.05))
    windows = [math.sqrt(sum(value*value for value in samples[i:i+width]) / width)
               for i in range(0, len(samples)-width+1, width)]
    reference = windows[4] if len(windows) > 4 else 0
    if reference <= 1e-7: return {"status": "no_reference_signal", "seconds_from_reference": None}
    threshold = reference * 0.1
    for index in range(5, len(windows)-2):
        if all(value <= threshold for value in windows[index:index+3]):
            return {"status": "measured", "seconds_from_reference": (index-4)*width/rate,
                    "reference_rms": reference, "rms_window_frames": width,
                    "definition": "20dB RMS drop from 200..250ms reference, three consecutive windows; not a broadband T60"}
    return {"status": "not_reached", "seconds_from_reference": None, "reference_rms": reference}


def read_mono(path: Path) -> tuple[list[float], int]:
    with wave.open(str(path), "rb") as source:
        if source.getnchannels() != 1 or source.getsampwidth() != 2 or source.getcomptype() != "NONE":
            raise ValueError("Native probe expected mono PCM16")
        rate = source.getframerate()
        raw = source.readframes(source.getnframes())
    pcm = array("h", raw)
    if sys.byteorder != "little": pcm.byteswap()
    return [value / 32768.0 for value in pcm], rate


def run_probe(root: Path, output: Path, profile: str, models: list[str], machine: str, sample_rate: int = 16000) -> dict:
    output = (root / output).resolve()
    if not output.is_relative_to((root / ".local").resolve()): raise ValueError("Probe reports/audio must stay under ignored .local/")
    if sample_rate < 8000 or sample_rate > 192000: raise ValueError("Probe sample rate must be in 8000..192000")
    if not models or any(model not in MODELS for model in models): raise ValueError("Unsupported probe model")
    output.parent.mkdir(parents=True, exist_ok=True)
    artifacts = output.parent / (output.stem + "-inputs-" + uuid.uuid4().hex[:12])
    artifacts.mkdir()
    before = dev.source_state(root)
    cli = dev.executable(root, profile, "gtracker_cli")
    version = json.loads(subprocess.run([str(cli), "--version", "--json"], cwd=root, capture_output=True, encoding="utf-8", check=True).stdout)
    results = []
    for model in models:
        for midi in (40, 52, 64):
            for fret in (0, 7):
                stem = f"{model}-midi{midi}-fret{fret}"
                pattern = {"schema_version": 2, "name": "Original known-pitch single-pluck probe", "revision": 1,
                           "tempo_bpm": 120, "length_beats": 4, "seed": 42,
                           "instrument": {"model": model, "tuning_midi": [midi], "decay_seconds": 4, "brightness": 0.78, "body_mix": 0},
                           "events": [{"id": "note", "type": "pluck", "beat": 0, "string": 1, "fret": fret, "strength": 0.7}], "gestures": []}
                score = artifacts / (stem + ".gtrk.json"); audio = artifacts / (stem + ".wav")
                score.write_text(json.dumps(pattern, indent=2) + "\n", encoding="utf-8")
                command = [str(cli), "render", str(score), "--output", str(audio), "--sample-rate", str(sample_rate), "--tail", "3", "--bypass-production", "--json"]
                started = time.perf_counter()
                native = json.loads(subprocess.run(command, cwd=root, capture_output=True, encoding="utf-8", check=True, timeout=30).stdout)
                wall_ms = (time.perf_counter()-started)*1000
                samples, rate = read_mono(audio)
                expected = 440 * 2**((midi+fret-69)/12)
                results.append({"model": model, "tuning_midi": midi, "fret": fret, "expected_hz": expected, "command": command,
                                "native": native, "process_wall_ms": wall_ms, "wav_sha256": hashlib.sha256(audio.read_bytes()).hexdigest(),
                                "known_pitch": known_pitch(samples[round(rate*.2):round(rate*.5)], rate, expected),
                                "decay_20db": decay_20db(samples, rate)})
    after = dev.source_state(root)
    if before["fingerprint_sha256"] != after["fingerprint_sha256"]: raise RuntimeError("Source changed during model probes; repeat against stable source")
    report = {"schema": "gtracker.model-probe", "version": 1, "created_utc": dev.utc_now(), "machine_label": machine,
              "platform": platform.platform(), "processor": platform.processor(), "renderer": version, "source_before": before,
              "source_after": after, "results": results,
              "limitations": ["Generated known-score isolated-string probes only; no real guitar/listening evidence",
                              "Pitch search is conditioned on the declared score and is not a transcription capability",
                              "RMS decay threshold is not calibrated broadband T60; PCM16 quantization bounds quiet measurements",
                              "Native generation and process/export cost are separate; one timing per case, no inferred speedup"]}
    dev.atomic_json(output, report)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(".local/model-probe/report.json"))
    parser.add_argument("--profile", choices=dev.PROFILES, default="headless")
    parser.add_argument("--model", choices=MODELS, action="append")
    parser.add_argument("--sample-rate", type=int, default=16000)
    parser.add_argument("--machine-label", default="Unlabelled local machine; inspect platform/processor fields")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        report = run_probe(dev.ROOT, args.output, args.profile, args.model or list(MODELS), args.machine_label, args.sample_rate)
        summary = {"ok": True, "output": str(args.output), "cases": len(report["results"]), "renderer": report["renderer"]}
        print(json.dumps(summary) if args.json else f"Measured {summary['cases']} generated known-score probes; see {args.output}")
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Model probe failed: {error}", file=sys.stderr); return 1


if __name__ == "__main__":
    dev.utf8_stdio()
    raise SystemExit(main())
