"""Explicit capture CLI with generated files and SDL dummy input only."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import unittest
import wave

from tools import dev

ROOT = Path(__file__).resolve().parents[1]
CAPTURE = Path(os.environ.get("GTRACKER_TEST_CAPTURE", dev.executable(ROOT, "desktop", "gtracker_capture", require=False)))


def generated_input(path: Path, rate: int, channels: int, frames: int) -> bytes:
    # Signed extrema and distinct channels expose accidental float normalization.
    values = [-32768, 32767, -1, 0, 1, 16384, -8192]
    data = b"".join(struct.pack("<h", values[(frame * 3 + channel) % len(values)])
                    for frame in range(frames) for channel in range(channels))
    with wave.open(str(path), "wb") as output:
        output.setframerate(rate)
        output.setnchannels(channels)
        output.setsampwidth(2)
        output.writeframes(data)
    return data


@unittest.skipUnless(CAPTURE.is_file(), "Native capture CLI is not built; build desktop for generated/dummy capture checks")
class CaptureTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / ".local/tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.pattern = self.directory / "äänitys-音.gtrk.json"
        pattern = json.loads((ROOT / "examples/campfire.gtrk.json").read_text(encoding="utf-8"))
        pattern.update(tempo_bpm=120, length_beats=1, events=[])
        dev.atomic_json(self.pattern, pattern)
        self.source = self.directory / "alkuperäinen-原音.wav"
        self.pcm = generated_input(self.source, 48000, 2, 96000)
        self.output = self.directory / "capture-音"
        self.environment = {**os.environ, "SDL_AUDIODRIVER": "not_a_real_driver"}

    def command(self, *args, output=None):
        return [str(CAPTURE), "record", "--pattern", str(self.pattern), "--output", str(output or self.output),
                "--pre-roll", "0.1", "--count-in", "1", "--tail", "0.1", "--job-id", "generated-job", "--json", *args]

    def run_capture(self, *args, code=0, output=None, environment=None):
        result = subprocess.run(self.command(*args, output=output), capture_output=True, encoding="utf-8",
                                env=environment or self.environment, timeout=15, check=False)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def test_generated_44100_48000_mono_stereo_retains_received_pcm_and_nominal_timing(self):
        for rate in (44100, 48000):
            for channels in (1, 2):
                with self.subTest(rate=rate, channels=channels):
                    data = generated_input(self.source, rate, channels, rate * 2)
                    original = self.source.read_bytes()
                    output = self.directory / f"take-{rate}-{channels}"
                    report = self.run_capture("--simulate", str(self.source), output=output)
                    self.assertEqual(report["source_kind"], "file_simulation")
                    self.assertEqual(report["audio_driver"], "none")
                    self.assertFalse(report["reference_played"])
                    self.assertFalse(report["format_conversion"])
                    self.assertEqual(report["source_pattern_revision"], 1)
                    self.assertEqual(report["job_id"], "generated-job")
                    self.assertEqual(report["simulation_input_sha256"], hashlib.sha256(original).hexdigest())
                    self.assertEqual(self.source.read_bytes(), original)
                    timing = report["timing"]
                    self.assertEqual(timing["pattern_zero_frame"], round(rate * 0.6))
                    self.assertEqual(timing["planned_frames"], round(rate * 1.2))
                    self.assertEqual(timing["captured_frames"], timing["planned_frames"])
                    self.assertEqual(timing["offset_status"], "uncalibrated")
                    self.assertEqual(timing["input_offset_frames"], 0)
                    with wave.open(str(output / "source.wav"), "rb") as recorded:
                        self.assertEqual((recorded.getframerate(), recorded.getnchannels(), recorded.getsampwidth()), (rate, channels, 2))
                        self.assertEqual(recorded.readframes(recorded.getnframes()), data[:timing["captured_frames"] * channels * 2])
                    self.assertEqual(json.loads((output / "capture.json").read_text(encoding="utf-8")), report)
                    self.assertEqual(json.loads((output / "pattern.gtrk.json").read_text(encoding="utf-8")), json.loads(self.pattern.read_text(encoding="utf-8")))

    def test_cancel_disconnect_and_exhaustion_retain_partial_samples_with_failure_status(self):
        cases = [("cancel", ["--simulate-cancel-frame", "1024"], 130, "cancelled", 1024),
                 ("disconnect", ["--simulate-disconnect-frame", "1234"], 1, "disconnected", 1234)]
        for name, flags, code, status, frames in cases:
            output = self.directory / name
            report = self.run_capture("--simulate", str(self.source), *flags, code=code, output=output)
            self.assertFalse(report["ok"])
            self.assertEqual(report["status"], status)
            self.assertEqual(report["source"]["frames"], frames)
            with wave.open(str(output / "source.wav"), "rb") as recorded:
                self.assertEqual(recorded.readframes(frames), self.pcm[:frames * 4])
        generated_input(self.source, 48000, 2, 300)
        report = self.run_capture("--simulate", str(self.source), code=1)
        self.assertEqual(report["status"], "source_exhausted")
        self.assertEqual(report["source"]["frames"], 300)

    def test_realtime_cancel_file_and_early_cancel_have_bounded_recovery(self):
        cancel_file = self.directory / "cancel"
        process = subprocess.Popen(self.command("--simulate", str(self.source), "--realtime-simulation", "--cancel-file", str(cancel_file)),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", env=self.environment)
        try:
            deadline = time.monotonic() + 5
            while not (self.output / "pattern.gtrk.json").is_file() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.02)
            time.sleep(0.15)
            cancel_file.touch()
            stdout, stderr = process.communicate(timeout=5)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
        self.assertEqual(process.returncode, 130, stdout + stderr)
        report = json.loads(stdout)
        self.assertGreater(report["source"]["frames"], 0)
        self.assertLess(report["source"]["frames"], report["timing"]["planned_frames"])
        output = self.directory / "zero"
        report = self.run_capture("--simulate", str(self.source), "--cancel-file", str(cancel_file), output=output, code=130)
        self.assertIsNone(report["source"])
        self.assertFalse((output / "source.wav").exists())

    def test_no_implicit_device_or_overwrite_and_errors_stay_json(self):
        report = self.run_capture(code=2)
        self.assertFalse(report["ok"])
        self.assertFalse(self.output.exists())
        self.run_capture("--simulate", str(self.source), "--device", "default", code=2)
        self.assertFalse(self.output.exists())
        self.output.mkdir()
        keep = self.output / "keep"
        keep.write_bytes(b"keep user work")
        self.run_capture("--simulate", str(self.source), code=2)
        self.assertEqual(keep.read_bytes(), b"keep user work")
        missing = self.directory / "missing-音.wav"
        report = self.run_capture("--simulate", str(missing), output=self.directory / "failure", code=1)
        self.assertEqual(report["status"], "failed")
        self.assertIsNone(report["source"])
        self.assertNotEqual(report["status"], "complete")
        report = self.run_capture("--device", "missing-input-音", output=self.directory / "missing-device", environment={**os.environ, "SDL_AUDIODRIVER": "dummy"}, code=1)
        self.assertEqual(report["status"], "failed")
        self.assertIn("not found", report["error"])

    def test_sdl_dummy_device_is_labelled_simulated_and_retains_application_format(self):
        environment = {**os.environ, "SDL_AUDIODRIVER": "dummy"}
        result = subprocess.run([str(CAPTURE), "devices", "--json"], capture_output=True, encoding="utf-8", env=environment, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        devices = json.loads(result.stdout)["devices"]
        self.assertGreaterEqual(len(devices), 1)
        report = self.run_capture("--device", "0", "--sample-rate", "44100", "--channels", "2", "--silent", environment=environment)
        self.assertEqual(report["source_kind"], "sdl_simulated_driver")
        self.assertEqual(report["audio_driver"], "dummy")
        self.assertEqual(report["source"]["sample_rate_hz"], 44100)
        self.assertEqual(report["source"]["channels"], 2)
        self.assertFalse(report["reference_played"])
        report = self.run_capture("--device", "default", "--metronome", output=self.directory / "dummy-clicks", environment=environment)
        self.assertTrue(report["reference_played"])
        self.assertTrue(report["metronome"])
        self.assertEqual(report["audio_driver"], "dummy")
        with wave.open(str(self.directory / "dummy-clicks/reference.wav"), "rb") as reference:
            samples = struct.unpack("<" + "h" * reference.getnframes(), reference.readframes(reference.getnframes()))
            self.assertTrue(any(samples))


if __name__ == "__main__":
    unittest.main()
