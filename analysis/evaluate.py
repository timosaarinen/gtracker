"""Repeat the original/generated monophonic experiment; no real-guitar verdict."""
from __future__ import annotations

import argparse
from array import array
import json
from pathlib import Path
import platform
import random
import subprocess
import sys
import time
import uuid
import wave

from analysis import propose
from tools import dev, takes

ROOT = Path(__file__).resolve().parents[1]


def score_predictions(notes: list[dict], labels: list[dict], rate: int) -> dict:
    """Unique pitch/onset matches; fingering coverage is not string recognition."""
    remaining = set(range(len(notes)))
    matches = []
    for label in labels:
        possible = [index for index in remaining
                    if abs(notes[index]["onset_frame"]-label["onset_frame"]) <= rate*.12
                    and abs(notes[index]["pitch_midi"]-label["pitch_midi"]) <= .5]
        if not possible:
            continue
        index = min(possible, key=lambda value: (abs(notes[value]["onset_frame"]-label["onset_frame"]), value))
        remaining.remove(index)
        note = notes[index]
        matches.append({"note_index": index, "label": label,
                        "onset_error_ms": (note["onset_frame"]-label["onset_frame"])/rate*1000,
                        "pitch_error_cents": (note["pitch_midi"]-label["pitch_midi"])*100,
                        "known_fingering_in_alternatives": any(alternative["string"] == label["string"]
                            and abs(alternative["fret"]-label["fret"]) <= .5 for alternative in note["alternatives"]),
                        "alternative_count": len(note["alternatives"])})
    return {"precision": len(matches)/len(notes) if notes else None,
            "recall": len(matches)/len(labels) if labels else None,
            "false_positives": len(remaining), "missed_notes": len(labels)-len(matches),
            "unmatched_note_indices": sorted(remaining), "matches": matches}


def variant(source: Path, output: Path, *, width: int, channels: int, noise: float = 0, silent: bool = False) -> None:
    with wave.open(str(source), "rb") as audio:
        rate = audio.getframerate()
        samples = array("h", audio.readframes(audio.getnframes()))
    if sys.byteorder != "little": samples.byteswap()
    rng = random.Random(42)
    raw = bytearray()
    scale = 1 << (width*8-1)
    for sample in samples:
        value = 0 if silent else max(-1, min(1, sample/32768+rng.gauss(0, noise)))
        encoded = max(-scale, min(scale-1, round(value*scale))).to_bytes(width, "little", signed=True)
        raw.extend(encoded*channels)
    with wave.open(str(output), "wb") as audio:
        audio.setparams((channels, width, rate, 0, "NONE", "not compressed"))
        audio.writeframes(raw)


def evaluate(output: Path, *, profile: str = "headless") -> dict:
    if output.is_symlink() or output.exists(): raise ValueError("Evaluation output already exists; choose a new directory")
    output = output.resolve()
    local = (ROOT/".local").resolve()
    if output == local or not output.is_relative_to(local):
        raise ValueError("Evaluation needs a new directory inside ignored .local/")
    # An existing or symlinked directory is never merged, cleared or overwritten.
    output.mkdir(parents=True, exist_ok=False)
    run_id=uuid.uuid4().hex
    report = {"schema": "gtracker.proposal-evaluation", "version": 1, "run_id": run_id, "status": "running",
              "started_utc": dev.utc_now(), "profile": profile,
              "environment": {"platform": platform.platform(), "machine": platform.machine(),
                              "processor": platform.processor(), "python": platform.python_version()},
              "matching_rule": "Nearest compatible onset in authored-label order, unique pitch within 50 cents AND source onset within 120 ms; labels are used only for evaluation/reference review",
              "cases": [], "commands": [],
              "limits": ["Original/generated fixtures only; no real guitar, microphone or listening qualification",
                         "Single-note assumption; unsupported chord output is retained without accuracy metrics",
                         "Fingering alternatives are possibilities, not detected strings; no technique recognition",
                         "Completed means the experiment ran, not that proposal quality passed"]}
    cli = dev.executable(ROOT, profile, "gtracker_cli")

    def native(*arguments):
        command = [str(cli), *map(str, arguments), "--json"]
        started = time.perf_counter()
        result = subprocess.run(command, cwd=ROOT, capture_output=True, encoding="utf-8", errors="replace", timeout=60)
        report["commands"].append({"argv": command, "returncode": result.returncode,
                                   "wall_ms": (time.perf_counter()-started)*1000,
                                   "stdout": result.stdout, "stderr": result.stderr})
        if result.returncode: raise RuntimeError("Native evaluation command failed: "+result.stderr+result.stdout)
        return json.loads(result.stdout)

    try:
        before = dev.source_state(ROOT); report["source_before"] = before
        report["renderer"] = native("--version")
        score_path = ROOT/"examples/monophonic-study.gtrk.json"
        authored = json.loads(score_path.read_text(encoding="utf-8"))
        report["authored_score_sha256"] = takes.file_hash(score_path)
        blank = dict(authored); blank["events"] = []
        pattern_path = output/"empty-pattern.gtrk.json"; dev.atomic_json(pattern_path, blank)
        cases = []
        for rate in (16000, 44100, 48000):
            source = output/f"clean-{rate}.wav"
            native("render", score_path, "--sample-rate", rate, "--tail", .5, "--output", source)
            cases.append((f"clean-{rate}", source, rate, 1, True, False, {"sample_width": 2, "channels": 1}))
        clean_48 = output/"clean-48000.wav"
        for name, width, channels, noise, silent in (("stereo24",3,2,0,False), ("noise-std003",2,1,.003,False), ("silence",2,1,0,True)):
            source = output/(name+".wav")
            variant(clean_48, source, width=width, channels=channels, noise=noise, silent=silent)
            cases.append((name, source, 48000, channels, True, silent,
                          {"sample_width": width, "channels": channels, "noise_std": noise, "noise_seed": 42,
                           "stereo_construction": "Identical copies of the original mono channel" if channels == 2 else None}))
        chord = dict(authored)
        chord["events"] = authored["events"]+[{"id": f"chord-{index}", "type": "pluck", "beat": beat,
                                              "string": 5, "fret": 2, "strength": .7} for index,beat in enumerate((0,2,4,6))]
        chord_path = output/"unsupported-chord.gtrk.json"; dev.atomic_json(chord_path, chord)
        chord_source = output/"unsupported-chord.wav"
        native("render", chord_path, "--sample-rate",48000,"--tail",.5,"--output",chord_source)
        cases.append(("unsupported-chord",chord_source,48000,1,False,False,{"reason": "Overlapping two-string notes violate the explicit single-note assumption"}))
        for name, source, rate, channel, qualifies, silent, condition in cases:
            source_hash = takes.file_hash(source)
            bundle = output/(name+"-take")
            takes.import_take(source,pattern_path,bundle,selection_end=rate*4,job_id="evaluation-"+run_id+"-"+name,profile=profile)
            started = time.perf_counter()
            proposals = propose.propose_take(bundle/"take.json",channel=channel,job_id="proposal-"+run_id+"-"+name,profile=profile)
            elapsed = (time.perf_counter()-started)*1000
            job = bundle/"proposals.json"; dev.atomic_json(job,proposals)
            native("proposal-info",bundle/"take.json","--proposals",job)
            labels = [] if silent else [{"onset_frame": round(event["beat"]*60/authored["tempo_bpm"]*rate),
                         "pitch_midi": authored["instrument"]["tuning_midi"][event["string"]-1]+event["fret"],
                         "string": event["string"], "fret": event["fret"], "beat": event["beat"], "strength": event["strength"]}
                        for event in authored["events"] if event["type"] == "pluck"]
            metrics = score_predictions(proposals["notes"],labels,rate) if qualifies else None
            result = {"name": name, "condition": condition, "qualifies_monophonic_assumption": qualifies,
                      "source_sha256": source_hash, "source_rate": rate, "analysis_channel": channel,
                      "status": proposals["status"], "candidate_count": len(proposals["notes"]),
                      "job_elapsed_ms": elapsed, "job": str(job.relative_to(output)),
                      "engine_provenance": proposals["provenance"], "labels": labels if qualifies else [],
                      "metrics": metrics}
            report["cases"].append(result)
            if name == "clean-48000" and metrics["missed_notes"] == 0:
                choices = []
                for match in metrics["matches"]:
                    note = proposals["notes"][match["note_index"]]; label = match["label"]
                    alternative = next((i+1 for i,a in enumerate(note["alternatives"])
                                        if a["string"] == label["string"] and abs(a["fret"]-label["fret"]) <= .5), None)
                    if alternative is None: break
                    choices.append({"note_id": note["id"], "alternative": alternative})
                if len(choices) == len(labels):
                    selection = {"schema": "gtracker.proposal-selection", "version": 1, "job_id": proposals["job_id"],
                                 "strength": .7, "choices": choices}
                    selection_path = output/"reference-review-selection.json"; dev.atomic_json(selection_path,selection)
                    reviewed = output/"reference-reviewed.gtrk.json"
                    native("accept-proposals",bundle/"take.json","--proposals",job,"--selection",selection_path,"--output",reviewed)
                    edited = json.loads(reviewed.read_text(encoding="utf-8"))
                    for event, match in zip(edited["events"],metrics["matches"]):
                        for field in ("beat","string","fret","strength"): event[field] = match["label"][field]
                    edited["events"] += [event for event in authored["events"] if event["type"] == "damp"]
                    edited["events"].sort(key=lambda event:event["beat"]); edited["revision"] += 1
                    dev.atomic_json(reviewed,edited)
                    resynthesis = output/"reference-resynthesis.wav"
                    native("render",reviewed,"--sample-rate",48000,"--tail",.5,"--output",resynthesis)
                    report["reference_review"] = {"method": "Oracle demonstration: known authored labels select matched candidates/fingerings, reject extras, restore pitch/onsets/strength and add authored damping; this is neither inference nor human review",
                        "selected_notes": len(choices), "rejected_candidate_indices": metrics["unmatched_note_indices"],
                        "selection": selection_path.name, "pattern": reviewed.name, "source": source.name,
                        "resynthesis": resynthesis.name, "source_pcm16_sha256": source_hash,
                        "resynthesis_sha256": takes.file_hash(resynthesis), "byte_identical_after_known_label_corrections": source.read_bytes() == resynthesis.read_bytes(),
                        "included_in_accuracy_metrics": False}
            if takes.file_hash(source) != source_hash or takes.file_hash(bundle/"source.wav") != source_hash:
                raise RuntimeError("Evaluation source audio changed")
        after = dev.source_state(ROOT); report["source_after"] = after
        if after["fingerprint_sha256"] != before["fingerprint_sha256"]: raise RuntimeError("Source changed during evaluation")
        report["status"] = "completed"
    except BaseException as error:
        report.update(status="interrupted" if isinstance(error,(KeyboardInterrupt,SystemExit)) else "failed",error=str(error))
        raise
    finally:
        report["finished_utc"] = dev.utc_now()
        dev.atomic_json(output/"report.json",report)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--profile",choices=dev.PROFILES,default="headless")
    parser.add_argument("--json",action="store_true")
    args = parser.parse_args(argv)
    try:
        report = evaluate(args.output,profile=args.profile)
        result = {"ok": True, "status": report["status"], "report": str(args.output.resolve()/"report.json"),
                  "cases": [{"name": case["name"], "candidates": case["candidate_count"],
                             "metrics": {key:case["metrics"][key] for key in ("precision","recall","false_positives","missed_notes")} if case["metrics"] is not None else None}
                            for case in report["cases"]]}
        print(json.dumps(result) if args.json else "Completed generated proposal evaluation: "+result["report"])
        return 0
    except (OSError,ValueError,RuntimeError,wave.Error,EOFError,subprocess.SubprocessError) as error:
        print("evaluation error: "+str(error),file=sys.stderr); return 1


if __name__ == "__main__":
    dev.utf8_stdio()
    raise SystemExit(main())
