"""Original, bounded YIN-style monophonic research baseline. No model weights.

Difference/CMND/first-trough/interpolation follow de Cheveigne & Kawahara,
JASA 111 (2002), DOI 10.1121/1.1458024. This is not their reference code or a
complete implementation of every refinement in that paper. Scores are not
calibrated probabilities. No polyphony or articulation classification occurs.
"""
from __future__ import annotations

import math
import statistics
from collections.abc import Sequence

ENGINE = "gtracker.monophonic-yin"
VERSION = "0.1.0"
MIN_HZ, MAX_HZ = 65.0, 1000.0


def pitch(frame: list[float], rate: float) -> tuple[float, float] | None:
    width = len(frame) // 2
    first, last = max(2, math.floor(rate / MAX_HZ)), math.ceil(rate / MIN_HZ)
    if width < last + 2: raise ValueError("Pitch frame is too short for the declared search range")
    difference = [0.0] * (last + 2)
    for lag in range(1, last + 2):
        total = 0.0
        for index in range(width):
            delta = frame[index] - frame[index + lag]
            total += delta * delta
        difference[lag] = total
    cumulative = 0.0
    normalized = [1.0] * len(difference)
    for lag in range(1, len(difference)):
        cumulative += difference[lag]
        normalized[lag] = difference[lag] * lag / cumulative if cumulative > 1e-20 else 1.0
    selected = None
    for lag in range(first, last + 1):
        if normalized[lag] < 0.15 and normalized[lag] <= normalized[lag-1] and normalized[lag] <= normalized[lag+1]:
            selected = lag; break
    if selected is None:
        selected = min(range(first, last+1), key=normalized.__getitem__)
    if normalized[selected] > 0.25 or selected in (first, last): return None
    a, b, c = difference[selected-1:selected+2]
    denominator = a - 2*b + c
    delta = max(-0.5, min(0.5, 0.5*(a-c)/denominator)) if denominator > 0 else 0.0
    hz = rate / (selected + delta)
    if not MIN_HZ <= hz <= MAX_HZ: return None
    return 69 + 12*math.log2(hz/440), max(0.0, min(1.0, 1-normalized[selected]))


def alternatives(midi: float, tuning: list[float]) -> list[dict]:
    result = [{"string": index+1, "fret": max(0.0, min(36.0, midi-open_midi))}
              for index, open_midi in enumerate(tuning) if -0.25 <= midi-open_midi <= 36.25]
    return sorted(result, key=lambda value: (value["fret"], value["string"]))


def propose(samples: Sequence[float], source_rate: int, selection_start: int, tuning: list[float]) -> tuple[list[dict], dict]:
    if source_rate < 8000 or source_rate > 192000 or not samples or len(samples) > source_rate * 30:
        raise ValueError("Monophonic proposals require a nonempty selection of at most 30 seconds at 8..192 kHz")
    # A bounded analysis copy only. Original PCM, rate, channels and timing remain
    # untouched. This short box filter is deliberately not a high-quality SRC.
    divisor = max(1, source_rate // 8000)
    signal = [sum(samples[index:index+divisor])/divisor for index in range(0, len(samples)-divisor+1, divisor)]
    rate = source_rate / divisor
    hop = round(rate * 0.01)
    width = max(256, math.ceil(rate * 0.04))
    positions = list(range(0, len(signal), hop))
    rms = [math.sqrt(sum(value*value for value in signal[max(0, center-hop):min(len(signal), center+hop)]) /
                     max(1, min(len(signal), center+hop)-max(0, center-hop))) for center in positions]
    threshold = max(0.001, max(rms, default=0) * 0.03)
    frames = []
    padded = [0.0]*(width//2) + signal + [0.0]*(2*width)
    for center, energy in zip(positions, rms):
        frames.append(pitch(padded[center:center+2*width], rate) if energy >= threshold else None)
    attack = [False] * len(frames)
    for index in range(3, len(frames)):
        attack[index] = rms[index] > max(threshold*3, sum(rms[index-3:index])/3 * 1.8, max(rms)*0.12)
    notes, run = [], []
    maximum_gap = 2

    def finish():
        if len(run) < 8 or positions[run[-1]]-positions[run[0]] < rate * 0.07: return
        values = [frames[index][0] for index in run]
        midi = statistics.median(values)
        if max(values)-min(values) > 1.0: return
        start_index = run[0]
        # Recover a nearby energy rise before stable periodicity. This is coarse
        # acoustic timing, with explicit uncertainty, never grid quantization.
        for index in range(run[0], max(-1, run[0]-9), -1):
            if rms[index] < threshold:
                start_index = index+1; break
            if attack[index]: start_index = index; break
        coarse = max(0, positions[start_index]-hop) * divisor
        # Refine the coarse energy edge in the selected original-rate channel.
        # This avoids placing an energy window's look-ahead before beat zero.
        # Existing ringing/noise can still obscure an attack; uncertainty stays.
        stop = min(len(samples), (positions[run[0]]+hop)*divisor)
        onset = next((index for index in range(coarse, stop) if abs(samples[index]) >= threshold*2), coarse) + selection_start
        end = min(len(samples), (positions[run[-1]]+hop)*divisor) + selection_start
        notes.append({"id": "note-" + str(len(notes)+1), "onset_frame": onset, "end_frame": end,
                      "pitch_midi": midi, "periodicity": statistics.mean(frames[index][1] for index in run),
                      "voiced_fraction": len(run)/(run[-1]-run[0]+1),
                      "pitch_spread_cents": (max(values)-min(values))*100,
                      "onset_uncertainty_frames": math.ceil(max(.1,(width+math.ceil(rate/MIN_HZ))/rate)*source_rate),
                      "alternatives": alternatives(midi, tuning)})

    for index, estimate in enumerate(frames):
        if estimate is None:
            if run and index-run[-1] > maximum_gap:
                finish(); run = []
            continue
        changed_pitch = run and abs(estimate[0]-statistics.median(frames[i][0] for i in run[-5:])) > 0.65
        repluck = run and len(run) >= 8 and attack[index] and positions[index]-positions[run[0]] > rate*0.1
        if changed_pitch or repluck:
            finish(); run = []
        run.append(index)
    finish()
    if len(notes) > 128: raise ValueError("More than 128 note candidates; choose a shorter/sparser selection")
    parameters = {"min_hz": MIN_HZ, "max_hz": MAX_HZ, "analysis_rate_hz": rate, "box_decimation_factor": divisor,
                  "difference_window_samples": width, "hop_samples": hop, "rms_gate": threshold,
                  "minimum_voiced_frames": 8, "maximum_gap_frames": maximum_gap, "default_render_strength": 0.6,
                  "assumed_monophonic": True, "technique_recognition": False, "timing_quantized": False}
    return notes, parameters
