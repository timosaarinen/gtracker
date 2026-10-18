"""Capture-to-take authoring, immutable jobs and retained recovery; no hardware."""

from __future__ import annotations

from copy import deepcopy
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock
import wave

from tools import dev, record, takes

ROOT = Path(__file__).resolve().parents[1]
CAPTURE = dev.executable(ROOT, "desktop", "gtracker_capture", require=False)
CLI = dev.executable(ROOT, "desktop", "gtracker_cli", require=False)


@unittest.skipUnless(CAPTURE.is_file() and CLI.is_file(), "Build desktop before generated capture-to-take checks")
class RecordTakeTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / ".local/tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.pattern = self.directory / "pattern-音.gtrk.json"
        self.score = json.loads((ROOT / "examples/campfire.gtrk.json").read_text(encoding="utf-8"))
        self.score.update(tempo_bpm=120, length_beats=1, events=[])
        dev.atomic_json(self.pattern, self.score)
        self.source = self.directory / "generated.wav"
        self.make_source(44100, 2)
        self.output = self.directory / "take-音"
        self.environment = mock.patch.dict(os.environ, {"SDL_AUDIODRIVER": "not_a_real_driver"})
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def make_source(self, rate, channels, seconds=2):
        with wave.open(str(self.source), "wb") as audio:
            audio.setframerate(rate)
            audio.setnchannels(channels)
            audio.setsampwidth(2)
            audio.writeframes(b"".join(struct.pack("<h", ((frame * 11 + channel * 9001) % 65536) - 32768)
                                      for frame in range(rate * seconds) for channel in range(channels)))

    def capture(self, **options):
        arguments = dict(simulate=self.source, count_in=1, pre_roll=0.1, tail=0.1, job_id="generated-workflow")
        arguments.update(options)
        return record.record_take(self.pattern, self.output, **arguments)

    def test_complete_bundle_reopens_with_matching_capture_analysis_and_nominal_beats(self):
        for rate, channels in ((44100, 2), (48000, 1)):
            with self.subTest(rate=rate, channels=channels):
                self.make_source(rate, channels)
                original = self.source.read_bytes()
                self.output = self.directory / f"take-{rate}-{channels}"
                result = self.capture()
                self.assertTrue(result["ok"], result)
                self.assertIsNone(result["recovery_path"])
                self.assertFalse(list(self.directory.glob(self.output.name + ".recovery-*")))
                take = takes.read_json(Path(result["take"]))
                capture = takes.read_json(self.output / "capture.json")
                analysis = takes.read_json(self.output / "analysis.json")
                self.assertEqual(take["id"], capture["job_id"])
                self.assertEqual(analysis["job_id"], capture["job_id"])
                self.assertEqual(take["source"]["sha256"], capture["source"]["sha256"])
                self.assertEqual(take["source_pattern_revision"], self.score["revision"])
                self.assertEqual(take["alignment"]["pattern_zero_frame"], round(rate * 0.6))
                self.assertEqual(takes.source_frame_to_beat(round(rate * 0.6), take), 0)
                self.assertEqual(take["alignment"]["offset_status"], "uncalibrated")
                self.assertEqual(takes.inspect_take(Path(result["take"]), profile="desktop")["analysis_status"], "metadata_only")
                self.assertFalse(analysis["capabilities"]["note_transcription"])
                self.assertEqual((self.output / "pattern.gtrk.json").read_bytes(), self.pattern.read_bytes())
                self.assertEqual(self.source.read_bytes(), original)
                native = subprocess.run([str(CLI), "take-info", result["take"], "--json"], capture_output=True, encoding="utf-8", timeout=15)
                self.assertEqual(native.returncode, 0, native.stdout + native.stderr)
                self.assertEqual(json.loads(native.stdout)["source_sha256"], capture["source"]["sha256"])

    def test_cancel_disconnect_and_analysis_failure_keep_recovery_without_successful_take(self):
        for label, options, status, code in (("cancel", {"simulate_cancel_frame": 1024}, "cancelled", 130),
                                              ("disconnect", {"simulate_disconnect_frame": 1500}, "disconnected", 1)):
            self.output = self.directory / label
            result = self.capture(**options)
            self.assertFalse(result["ok"])
            self.assertEqual(result["exit_code"], code)
            self.assertEqual(result["capture_status"], status)
            self.assertFalse(self.output.exists())
            recovery = Path(result["recovery_path"])
            self.assertTrue((recovery / "raw/source.wav").is_file())
            self.assertTrue((recovery / "raw/capture.json").is_file())
            self.assertEqual(takes.read_json(recovery / "record.json")["capture_status"], status)
        self.output = self.directory / "analysis-failed"
        with mock.patch.object(takes, "import_take", side_effect=ValueError("injected analysis failure")):
            result = self.capture()
        self.assertFalse(result["ok"])
        self.assertEqual(result["capture_status"], "complete")
        self.assertEqual(result["analysis_status"], "failed")
        self.assertIn("injected analysis failure", result["error"])
        self.assertFalse(self.output.exists())
        self.assertTrue((Path(result["recovery_path"]) / "raw/source.wav").is_file())

    def test_controller_interrupt_requests_graceful_native_cancel_and_retains_context(self):
        start = subprocess.Popen
        children = []
        def launch(*args, **kwargs):
            process = start(*args, **kwargs)
            if Path(args[0][0]) != CAPTURE:
                return process
            children.append(process)
            communicate = process.communicate
            first = True
            def interrupt_once(*args, **kwargs):
                nonlocal first
                if first:
                    first = False
                    raise KeyboardInterrupt
                return communicate(*args, **kwargs)
            process.communicate = interrupt_once
            return process
        with mock.patch.object(record.subprocess, "Popen", side_effect=launch):
            result = self.capture(realtime_simulation=True)
        self.assertEqual(result["exit_code"], 130)
        self.assertFalse(result["ok"])
        self.assertEqual(result["capture_status"], "cancelled")
        recovery = Path(result["recovery_path"])
        self.assertTrue((recovery / "raw/capture.json").is_file())
        self.assertTrue((recovery / "raw/pattern.gtrk.json").is_file())
        self.assertTrue(all(process.poll() is not None for process in children))

    def test_changed_original_pattern_does_not_change_active_capture_job(self):
        original = self.pattern.read_bytes()
        results = []
        worker = threading.Thread(target=lambda: results.append(self.capture(realtime_simulation=True)))
        worker.start()
        try:
            deadline = time.monotonic() + 5
            while not list(self.directory.glob("*.recovery-*/raw/pattern.gtrk.json")) and worker.is_alive() and time.monotonic() < deadline:
                time.sleep(0.02)
            changed = deepcopy(self.score)
            changed.update(revision=2, tempo_bpm=100)
            dev.atomic_json(self.pattern, changed)
        finally:
            worker.join(timeout=8)
        self.assertFalse(worker.is_alive())
        self.assertTrue(results[0]["ok"], results)
        self.assertEqual((self.output / "pattern.gtrk.json").read_bytes(), original)
        self.assertEqual(takes.read_json(self.output / "analysis.json")["source_pattern_revision"], 1)
        self.assertEqual(takes.read_json(self.output / "take.json")["alignment"]["tempo_bpm"], 120)

    def test_receipt_identity_timing_source_and_reference_tampering_are_rejected(self):
        self.assertTrue(self.capture()["ok"])
        capture = takes.read_json(self.output / "capture.json")
        pattern_hash = takes.file_hash(self.output / "pattern.gtrk.json")
        mutations = [("version", 2), ("job_id", "old-job"), ("source_pattern_revision", True),
                     ("pattern_sha256", "0" * 64), ("reference_sha256", "0" * 64)]
        for key, value in mutations:
            modified = deepcopy(capture)
            modified[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                record.verify_completed_capture(self.output, modified, self.score, "generated-workflow", pattern_hash)
        for key, value in (("pattern_zero_frame", 3), ("offset_status", "measured"), ("captured_frames", 12), ("planned_frames", True)):
            modified = deepcopy(capture)
            modified["timing"][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                record.verify_completed_capture(self.output, modified, self.score, "generated-workflow", pattern_hash)
        (self.output / "source.wav").write_bytes((self.output / "source.wav").read_bytes() + b"changed")
        with self.assertRaisesRegex(ValueError, "source"):
            record.verify_completed_capture(self.output, capture, self.score, "generated-workflow", pattern_hash)

    def test_generated_loopback_reference_and_delay_keep_calibration_explicit(self):
        self.make_source(48000, 2, seconds=4)
        first = self.capture(count_in=4, calibration_probe=True)
        self.assertTrue(first["ok"], first)
        receipt = takes.read_json(self.output / "capture.json")
        self.assertEqual(receipt["reference_kind"], "broadband_probe")
        self.assertEqual(receipt["reference_seed"], self.score["seed"])
        with wave.open(str(self.output / "reference.wav"), "rb") as audio:
            reference = struct.unpack("<" + "h" * audio.getnframes(), audio.readframes(audio.getnframes()))
        delay = 137
        with wave.open(str(self.source), "wb") as audio:
            audio.setframerate(48000); audio.setnchannels(2); audio.setsampwidth(2)
            audio.writeframes(b"".join(struct.pack("<hh", reference[n - delay] if delay <= n < len(reference) + delay else 0,
                                                          -(reference[n - delay] if delay <= n < len(reference) + delay else 0))
                                      for n in range(len(reference) + delay)))
        self.output = self.directory / "simulated-loopback"
        second = self.capture(count_in=4, calibration_probe=True)
        self.assertTrue(second["ok"], second)
        aligned = subprocess.run([str(CLI), "align-audio", "--reference", str(self.output / "reference.wav"),
                                  "--recorded", str(self.output / "source.wav"), "--recorded-channel", "2", "--json"],
                                 capture_output=True, encoding="utf-8", timeout=15)
        self.assertEqual(aligned.returncode, 0, aligned.stdout + aligned.stderr)
        result = json.loads(aligned.stdout)
        self.assertEqual(result["offset_frames"], delay)
        self.assertEqual(result["polarity"], -1)
        self.assertFalse(result["physical_latency_measured"])
        self.assertEqual(takes.read_json(self.output / "take.json")["alignment"]["offset_status"], "uncalibrated")

    def test_existing_output_and_implicit_input_are_refused_before_launch(self):
        with mock.patch.object(record.subprocess, "Popen") as process:
            with self.assertRaisesRegex(ValueError, "exactly one"):
                record.record_take(self.pattern, self.output)
            with self.assertRaisesRegex(ValueError, "Job ID"):
                self.capture(job_id="")
            self.output.mkdir()
            marker = self.output / "keep"
            marker.write_bytes(b"user work")
            with self.assertRaisesRegex(ValueError, "already exists"):
                self.capture()
            process.assert_not_called()
            self.assertEqual(marker.read_bytes(), b"user work")

    def test_developer_command_returns_json_from_unrelated_working_directory(self):
        command = [sys.executable, str(ROOT / "tools/dev.py"), "record-take", "--pattern", str(self.pattern),
                   "--output", str(self.output), "--simulate", str(self.source), "--count-in", "0", "--pre-roll", "0", "--tail", "0", "--json"]
        result = subprocess.run(command, cwd=self.directory, capture_output=True, encoding="utf-8", timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(json.loads(result.stdout)["ok"])


if __name__ == "__main__":
    unittest.main()
