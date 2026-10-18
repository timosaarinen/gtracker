"""Native file-delay CLI against generated broadband references, no hardware."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

from tools import dev

ROOT = Path(__file__).resolve().parents[1]
CLI = dev.executable(ROOT, "headless", "gtracker_cli", require=False)


@unittest.skipUnless(CLI.is_file(), "Build headless before native relative-delay checks")
class AlignmentCliTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / ".local/tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.reference = self.directory / "vertailu-参考.wav"
        self.recorded = self.directory / "viivästetty-遅延.wav"

    def cli(self, *arguments, code=0):
        result = subprocess.run([str(CLI), *map(str, arguments), "--json"], capture_output=True, encoding="utf-8", timeout=15)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def probe(self, rate=48000):
        return self.cli("calibration-probe", "--output", self.reference, "--sample-rate", rate, "--seed", 123)

    def delayed_pcm24(self, frames=137):
        with wave.open(str(self.reference), "rb") as audio:
            rate = audio.getframerate()
            samples = struct.unpack("<" + "h" * audio.getnframes(), audio.readframes(audio.getnframes()))
        data = bytearray()
        for n in range(len(samples) + rate // 2):
            source = n - frames
            value = samples[source] if 0 <= source < len(samples) else 0
            for channel in range(2):
                sample = round((-value * 0.7 + 500 + channel) * 256)
                data.extend(sample.to_bytes(3, "little", signed=True))
        with wave.open(str(self.recorded), "wb") as audio:
            audio.setframerate(rate)
            audio.setnchannels(2)
            audio.setsampwidth(3)
            audio.writeframes(data)

    def test_probe_and_delay_report_preserve_source_hashes_channels_and_sign(self):
        probe = self.probe(44100)
        self.assertEqual(probe["frames"], 88200)
        reference_bytes = self.reference.read_bytes()
        self.delayed_pcm24(137)
        recorded_bytes = self.recorded.read_bytes()
        output = self.directory / "offset.json"
        report = self.cli("align-audio", "--reference", self.reference, "--recorded", self.recorded,
                          "--recorded-channel", 2, "--max-offset-ms", 500, "--output", output)
        self.assertEqual(report["status"], "matched")
        self.assertEqual(report["offset_frames"], 137)
        self.assertAlmostEqual(report["offset_seconds"], 137 / 44100)
        self.assertEqual(report["polarity"], -1)
        self.assertGreater(report["fine_score"], 0.99)
        self.assertFalse(report["physical_latency_measured"])
        self.assertFalse(report["drift_measured"])
        self.assertEqual(report["recorded"]["selected_channel"], 2)
        self.assertEqual(report["recorded"]["sample_width_bits"], 24)
        self.assertEqual(report["reference"]["sha256"], hashlib.sha256(reference_bytes).hexdigest())
        self.assertEqual(report["recorded"]["sha256"], hashlib.sha256(recorded_bytes).hexdigest())
        self.assertEqual(json.loads(output.read_text(encoding="utf-8")), report)
        self.assertEqual(self.reference.read_bytes(), reference_bytes)
        self.assertEqual(self.recorded.read_bytes(), recorded_bytes)

    def test_silence_and_outside_search_return_no_applicable_offset(self):
        self.probe()
        self.delayed_pcm24(4800)
        report = self.cli("align-audio", "--reference", self.reference, "--recorded", self.recorded, "--max-offset-ms", 100, code=1)
        self.assertEqual(report["status"], "search_boundary")
        self.assertIsNone(report["offset_frames"])
        with wave.open(str(self.recorded), "wb") as audio:
            audio.setframerate(48000); audio.setnchannels(1); audio.setsampwidth(2)
            audio.writeframes(b"\0\0" * 96000)
        report = self.cli("align-audio", "--reference", self.reference, "--recorded", self.recorded, code=1)
        self.assertEqual(report["status"], "no_signal")
        self.assertIsNone(report["offset_frames"])

    def test_invalid_rate_channel_range_and_existing_output_are_refused(self):
        self.probe()
        self.delayed_pcm24()
        original = self.reference.read_bytes()
        self.cli("calibration-probe", "--output", self.reference, code=2)
        self.cli("align-audio", "--reference", self.reference, "--recorded", self.recorded, "--output", self.reference, code=2)
        self.assertEqual(self.reference.read_bytes(), original)
        for extra in (("--max-offset-ms", "0"), ("--max-offset-ms", "2001"), ("--window-seconds", "nan"), ("--reference-channel", "2")):
            self.cli("align-audio", "--reference", self.reference, "--recorded", self.recorded, *extra, code=2)
        with wave.open(str(self.recorded), "wb") as audio:
            audio.setframerate(44100); audio.setnchannels(1); audio.setsampwidth(2)
            audio.writeframes(b"\0\0" * 88200)
        error = self.cli("align-audio", "--reference", self.reference, "--recorded", self.recorded, code=2)
        self.assertIn("equal source rates", error["error"])

    def test_developer_alignment_commands_emit_utf8_json(self):
        probe = subprocess.run([sys.executable, str(ROOT / "tools/dev.py"), "calibration-probe", "--output", str(self.reference), "--json"],
                               cwd=self.directory, capture_output=True, encoding="utf-8", timeout=15)
        self.assertEqual(probe.returncode, 0, probe.stdout + probe.stderr)
        self.assertEqual(json.loads(probe.stdout)["output"], str(self.reference))
        self.delayed_pcm24(-127)
        result = subprocess.run([sys.executable, str(ROOT / "tools/dev.py"), "align-audio", "--reference", str(self.reference),
                                 "--recorded", str(self.recorded), "--json"], cwd=self.directory, capture_output=True, encoding="utf-8", timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout)["offset_frames"], -127)


if __name__ == "__main__":
    unittest.main()
