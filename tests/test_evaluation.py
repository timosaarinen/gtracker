"""Evaluation bookkeeping/ownership; generated experiment is run separately."""
from array import array
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import wave

from analysis import evaluate

ROOT=Path(__file__).resolve().parents[1]


class EvaluationTests(unittest.TestCase):
    def setUp(self):
        base=ROOT/".local/tests";base.mkdir(parents=True,exist_ok=True)
        temporary=tempfile.TemporaryDirectory(dir=base);self.addCleanup(temporary.cleanup)
        self.directory=Path(temporary.name)

    def test_extra_octave_candidate_is_a_false_positive_and_fingering_is_only_coverage(self):
        labels=[{"onset_frame":0,"pitch_midi":40,"string":6,"fret":0},
                {"onset_frame":1000,"pitch_midi":52,"string":6,"fret":12}]
        notes=[{"onset_frame":0,"pitch_midi":40.1,"alternatives":[{"string":6,"fret":0}]},
               {"onset_frame":995,"pitch_midi":40,"alternatives":[{"string":6,"fret":0}]},
               {"onset_frame":1010,"pitch_midi":52.1,"alternatives":[{"string":4,"fret":2},{"string":6,"fret":12}]}]
        result=evaluate.score_predictions(notes,labels,1000)
        self.assertEqual((result["precision"],result["recall"],result["false_positives"],result["missed_notes"]),(2/3,1,1,0))
        self.assertEqual(result["unmatched_note_indices"],[1])
        self.assertEqual(result["matches"][1]["onset_error_ms"],10)
        self.assertTrue(result["matches"][1]["known_fingering_in_alternatives"])
        self.assertEqual(result["matches"][1]["alternative_count"],2)

    def test_duplicate_candidates_are_unique_matches_and_empty_metrics_stay_undefined(self):
        label={"onset_frame":0,"pitch_midi":40,"string":6,"fret":0}
        note={"onset_frame":0,"pitch_midi":40,"alternatives":[{"string":6,"fret":0}]}
        result=evaluate.score_predictions([note,note],[label],1000)
        self.assertEqual((result["precision"],result["recall"],result["false_positives"]),(.5,1,1))
        empty=evaluate.score_predictions([],[],1000)
        self.assertIsNone(empty["precision"]);self.assertIsNone(empty["recall"])
        outside=evaluate.score_predictions([{**note,"onset_frame":121}],[label],1000)
        self.assertEqual((outside["false_positives"],outside["missed_notes"]),(1,1))

    def test_generated_stereo24_keeps_sign_channel_identity_and_original_bytes(self):
        source=self.directory/"original.wav";samples=array("h",[-32768,-3,0,3,32767])
        if sys.byteorder!="little":samples.byteswap()
        with wave.open(str(source),"wb") as audio:
            audio.setparams((1,2,48000,0,"NONE","not compressed"));audio.writeframes(samples.tobytes())
        before=source.read_bytes();stereo=self.directory/"stereo.wav"
        evaluate.variant(source,stereo,width=3,channels=2)
        with wave.open(str(stereo),"rb") as audio:
            self.assertEqual((audio.getnchannels(),audio.getsampwidth(),audio.getnframes()),(2,3,5))
            raw=audio.readframes(5)
        values=[int.from_bytes(raw[i:i+3],"little",signed=True) for i in range(0,len(raw),3)]
        self.assertEqual(values,[value*256 for value in (-32768,-32768,-3,-3,0,0,3,3,32767,32767)])
        self.assertEqual(source.read_bytes(),before)

    def test_output_ownership_rejects_existing_and_outside_local_without_changes(self):
        existing=self.directory/"existing";existing.mkdir();marker=existing/"keep.txt";marker.write_text("owned test fixture")
        with self.assertRaises(ValueError):evaluate.evaluate(existing)
        with self.assertRaises(ValueError):evaluate.evaluate(ROOT/"unowned-evaluation-output")
        self.assertEqual(marker.read_text(),"owned test fixture")
        self.assertFalse((ROOT/"unowned-evaluation-output").exists())

    def test_native_failure_retains_an_honest_partial_report_and_refuses_merge(self):
        output=self.directory/"failed-evaluation"
        state={"revision":"test","fingerprint_sha256":"test"}
        failure=subprocess.CompletedProcess(["owned-fake-cli"],2,"", "owned native failure fixture")
        with mock.patch.object(evaluate.dev,"source_state",return_value=state),mock.patch.object(evaluate.subprocess,"run",return_value=failure):
            with self.assertRaisesRegex(RuntimeError,"owned native failure"):evaluate.evaluate(output)
        report=json.loads((output/"report.json").read_text())
        self.assertEqual((report["schema"],report["version"],report["status"]),("gtracker.proposal-evaluation",1,"failed"))
        self.assertEqual(report["commands"][0]["returncode"],2)
        before=(output/"report.json").read_bytes()
        with self.assertRaises(ValueError):evaluate.evaluate(output)
        self.assertEqual((output/"report.json").read_bytes(),before)


if __name__=="__main__":unittest.main()
