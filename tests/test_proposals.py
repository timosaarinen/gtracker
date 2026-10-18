"""Original/generated monophonic proposals; never real-guitar qualification."""
from array import array
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import wave

from analysis import monophonic, propose
from tools import dev, takes

ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("GTRACKER_TEST_CLI", dev.executable(ROOT, "headless", "gtracker_cli")))


def tone(rate, seconds, hz):
    return [0.3 * (math.sin(2*math.pi*hz*i/rate) + 0.2*math.sin(2*math.pi*hz*2*i/rate)) for i in range(round(rate*seconds))]


class MonophonicMeasurements(unittest.TestCase):
    def test_blind_pitch_and_diagnostics_on_original_tones_noise_and_silence(self):
        for hz in (82.4069, 220.3, 659.255):
            result = monophonic.pitch(tone(8000, 0.08, hz), 8000)
            self.assertIsNotNone(result)
            self.assertLess(abs(result[0] - (69+12*math.log2(hz/440))), 0.06)
            self.assertGreater(result[1], 0.95)
        rng = random.Random(42)
        self.assertIsNone(monophonic.pitch([rng.uniform(-0.3, 0.3) for _ in range(640)], 8000))
        self.assertIsNone(monophonic.pitch([0.0]*640, 8000))

    def test_two_regions_keep_source_frames_pitch_and_ambiguous_fingering(self):
        rate = 8000
        samples = [0.0]*1600 + tone(rate, 0.5, 220) + [0.0]*2400 + tone(rate, 0.5, 329.6276) + [0.0]*1600
        notes, parameters = monophonic.propose(samples, rate, 12345, [64,59,55,50,45,40])
        self.assertEqual(len(notes), 2)
        for note, onset, midi in zip(notes, (0.2, 1.0), (57, 64)):
            self.assertAlmostEqual((note["onset_frame"]-12345)/rate, onset, delta=0.08)
            self.assertAlmostEqual(note["pitch_midi"], midi, delta=0.06)
            self.assertGreater(len(note["alternatives"]), 1)
            self.assertGreater(note["onset_uncertainty_frames"], 0)
        self.assertFalse(parameters["timing_quantized"])
        self.assertFalse(parameters["technique_recognition"])

    def test_silence_and_bounds_do_not_claim_unavailable_or_unlimited_processing(self):
        notes, _ = monophonic.propose([0.0]*8000, 8000, 0, [40])
        self.assertEqual(notes, [])
        with self.assertRaises(ValueError): monophonic.propose([], 8000, 0, [40])
        with self.assertRaises(ValueError): monophonic.propose([0.0]*240001, 8000, 0, [40])
        self.assertEqual(monophonic.alternatives(30, [40]), [])


@unittest.skipUnless(CLI.is_file(), "Native CLI not built")
class ProposalJobs(unittest.TestCase):
    def setUp(self):
        base = ROOT / ".local/tests"; base.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def bundle(self, rate=44100, silent=False):
        score = json.loads((ROOT / "examples/campfire.gtrk.json").read_text(encoding="utf-8"))
        score["events"] = []; score["tempo_bpm"] = 120; score["length_beats"] = 8
        pattern = self.directory / "original.gtrk.json"; pattern.write_text(json.dumps(score), encoding="utf-8")
        source = self.directory / "original.wav"
        left = [0.0]*round(rate*.15) + tone(rate, .5, 220) + [0.0]*round(rate*.15)
        right = [0.0]*round(rate*.15) + tone(rate, .5, 440) + [0.0]*round(rate*.15)
        pcm = array("h", (0 if silent else round(value*32767) for pair in zip(left, right) for value in pair))
        if sys.byteorder != "little": pcm.byteswap()
        with wave.open(str(source), "wb") as output:
            output.setparams((2,2,rate,0,"NONE","not compressed")); output.writeframes(pcm.tobytes())
        directory = self.directory / "take"
        takes.import_take(source, pattern, directory, zero_frame=round(rate*.15), job_id="original-two-channel")
        return directory / "take.json"

    def test_explicit_stereo_channel_has_independent_pitch_and_preserves_metadata_worker(self):
        path = self.bundle()
        before = {file.name: file.read_bytes() for file in path.parent.iterdir()}
        for channel, midi in ((1,57),(2,69)):
            report = propose.propose_take(path, channel=channel, job_id=f"proposal-{channel}")
            self.assertEqual(report["status"], "proposals")
            self.assertEqual(len(report["notes"]), 1)
            self.assertAlmostEqual(report["notes"][0]["pitch_midi"], midi, delta=.08)
            self.assertEqual(report["source"]["channel"], channel)
            self.assertEqual(report["take_id"], "original-two-channel")
            self.assertEqual(report["source_pattern_revision"], report["source_pattern"]["revision"])
        self.assertEqual(before, {file.name: file.read_bytes() for file in path.parent.iterdir()})
        self.assertEqual(json.loads(before["analysis.json"])["status"], "metadata_only")
        with self.assertRaises(ValueError): propose.propose_take(path, channel=3)

    def test_silent_48k_job_is_implemented_with_no_confident_candidates(self):
        path = self.bundle(48000, silent=True)
        report = propose.propose_take(path, channel=1)
        self.assertEqual(report["status"], "no_confident_candidates")
        self.assertEqual(report["notes"], [])

    def test_pattern_changed_during_job_and_oversized_snapshot_are_rejected(self):
        path = self.bundle()
        pattern_path = path.parent/"pattern.gtrk.json"
        implementation = monophonic.propose
        def edit_during_analysis(*args):
            value=json.loads(pattern_path.read_text(encoding="utf-8")); value["name"]="Changed during job"
            dev.atomic_json(pattern_path,value)
            return implementation(*args)
        with mock.patch.object(monophonic,"propose",side_effect=edit_during_analysis):
            with self.assertRaisesRegex(ValueError,"changed during proposals"): propose.propose_take(path,channel=1)
        oversized=self.directory/"oversized.json"; oversized.write_bytes(b" "*(1024*1024+1))
        with self.assertRaisesRegex(ValueError,"exceeds 1 MiB"): propose.snapshot(oversized)

    def test_cli_requires_assumption_and_preserves_existing_output(self):
        path = self.bundle(48000)
        output = self.directory / "proposals.json"
        command = [sys.executable, str(ROOT / "analysis/propose.py"), "--take", str(path), "--channel", "1", "--output", str(output), "--json"]
        result = subprocess.run(command, cwd=ROOT, capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertFalse(output.exists())
        command.append("--assume-monophonic")
        result = subprocess.run(command, cwd=ROOT, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        before = output.read_bytes()
        self.assertEqual(subprocess.run(command, cwd=ROOT, capture_output=True).returncode, 1)
        self.assertEqual(output.read_bytes(), before)

    def native(self, *args, ok=True):
        result = subprocess.run([str(CLI), *map(str,args), "--json"], cwd=ROOT, capture_output=True, encoding="utf-8")
        self.assertEqual(result.returncode == 0, ok, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def proposal_files(self, path):
        report = propose.propose_take(path, channel=1, job_id="review-this-job")
        proposal_path, selection_path = self.directory / "proposals.json", self.directory / "selection.json"
        dev.atomic_json(proposal_path, report)
        selection = {"schema":"gtracker.proposal-selection", "version":1, "job_id":report["job_id"], "strength":.6,
                     "choices":[{"note_id":report["notes"][0]["id"], "alternative":len(report["notes"][0]["alternatives"])}]}
        dev.atomic_json(selection_path, selection)
        return report, proposal_path, selection, selection_path

    def test_native_review_explicit_fingering_acceptance_and_render_keep_sources(self):
        path = self.bundle()
        before = {file.name:file.read_bytes() for file in path.parent.iterdir()}
        report, proposal_path, selection, selection_path = self.proposal_files(path)
        info = self.native("proposal-info", path, "--proposals", proposal_path)
        self.assertTrue(info["matches_current_pattern"])
        self.assertEqual(len(info["notes"]), 1)
        accepted = self.directory / "accepted.gtrk.json"
        result = self.native("accept-proposals", path, "--proposals", proposal_path, "--selection", selection_path, "--output", accepted)
        self.assertEqual(result["accepted_notes"], 1)
        pattern = json.loads(accepted.read_text(encoding="utf-8"))
        alternative = report["notes"][0]["alternatives"][-1]
        self.assertEqual(pattern["revision"], report["source_pattern_revision"]+1)
        self.assertEqual(pattern["events"][0]["string"], alternative["string"])
        self.assertAlmostEqual(pattern["events"][0]["fret"], alternative["fret"], places=5)
        self.assertAlmostEqual(pattern["events"][0]["beat"], takes.source_frame_to_beat(report["notes"][0]["onset_frame"], takes.read_json(path)), places=12)
        audio = self.native("render", accepted, "--output", self.directory / "accepted.wav")
        self.assertGreater(audio["peak"], 0)
        self.assertEqual(audio["clipped_samples"], 0)
        self.assertEqual(before, {file.name:file.read_bytes() for file in path.parent.iterdir()})
        for protected in (path, path.parent/"source.wav", path.parent/"pattern.gtrk.json", proposal_path, selection_path):
            self.native("accept-proposals", path, "--proposals", proposal_path, "--selection", selection_path, "--output", protected, ok=False)
        self.assertEqual(before, {file.name:file.read_bytes() for file in path.parent.iterdir()})

    def test_native_rejects_tampered_future_deep_and_stale_jobs_without_writes(self):
        path = self.bundle()
        original, proposal_path, _, selection_path = self.proposal_files(path)
        output = self.directory/"old.gtrk.json"; output.write_bytes(b"previous output")
        for change in (lambda x:x.update(version=2), lambda x:x.update(future=True),
                       lambda x:x["source"].update(sha256="b"*64), lambda x:x["alignment"].update(input_offset_frames=1),
                       lambda x:x["notes"][0].update(periodicity=2), lambda x:x["notes"][0].update(future_gesture="slide"),
                       lambda x:x["notes"][0]["alternatives"][0].update(fret=36), lambda x:x["provenance"].update(engine="unknown")):
            value = json.loads(json.dumps(original)); change(value); dev.atomic_json(proposal_path,value)
            self.native("accept-proposals", path, "--proposals", proposal_path, "--selection", selection_path, "--output", output, ok=False)
            self.assertEqual(output.read_bytes(), b"previous output")
        deep = json.loads(json.dumps(original)); nested = 0
        for _ in range(70): nested = [nested]
        deep["parameters"]["rms_gate"] = nested; dev.atomic_json(proposal_path,deep)
        self.native("proposal-info", path, "--proposals", proposal_path, ok=False)
        dev.atomic_json(proposal_path,original)
        pattern_path=path.parent/"pattern.gtrk.json"; pattern=json.loads(pattern_path.read_text(encoding="utf-8"))
        pattern["instrument"]["brightness"] = .9  # Even a same-revision external edit is stale.
        dev.atomic_json(pattern_path,pattern)
        self.assertFalse(self.native("proposal-info", path, "--proposals", proposal_path)["matches_current_pattern"])
        self.native("accept-proposals", path, "--proposals", proposal_path, "--selection", selection_path, "--output", output, ok=False)
        self.assertEqual(output.read_bytes(), b"previous output")

    def test_native_selection_is_explicit_versioned_and_atomic(self):
        path = self.bundle()
        _, proposal_path, original, selection_path = self.proposal_files(path)
        output=self.directory/"old.gtrk.json"; output.write_bytes(b"previous output")
        for change in (lambda x:x.update(job_id="different-job"), lambda x:x.update(choices=[]),
                       lambda x:x["choices"].append(x["choices"][0].copy()), lambda x:x["choices"][0].update(alternative=99),
                       lambda x:x.update(strength=2), lambda x:x.update(future=True)):
            value=json.loads(json.dumps(original)); change(value); dev.atomic_json(selection_path,value)
            self.native("accept-proposals", path, "--proposals", proposal_path, "--selection", selection_path, "--output", output, ok=False)
            self.assertEqual(output.read_bytes(), b"previous output")


if __name__ == "__main__": unittest.main()
