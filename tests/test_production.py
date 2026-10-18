"""Original fixture production comparisons; no hardware/listening inference."""
from copy import deepcopy
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
CLI = Path(os.environ.get("GTRACKER_TEST_CLI", dev.executable(ROOT, "headless", "gtracker_cli")))


@unittest.skipUnless(CLI.is_file(), "Native CLI not built")
class ProductionTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / ".local/tests"
        directory.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=directory)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source = ROOT / "examples/campfire.gtrk.json"
        self.settings = json.loads((ROOT / "examples/room.json").read_text(encoding="utf-8"))

    def cli(self, *arguments, success=True):
        process = subprocess.run([str(CLI), *map(str, arguments), "--json"], cwd=ROOT, encoding="utf-8", capture_output=True)
        self.assertEqual(process.returncode, 0 if success else 2, process.stderr or process.stdout)
        return json.loads(process.stdout)

    def test_original_dry_and_external_wet_are_repeatable_with_correct_stereo_tail(self):
        source_bytes = self.source.read_bytes()
        dry_path, wet_path = self.directory / "dry.wav", self.directory / "wet.wav"
        dry = self.cli("render", self.source, "--output", dry_path)
        wet = self.cli("render", self.source, "--production", ROOT / "examples/room.json", "--output", wet_path)
        self.assertEqual(hashlib.sha256(dry_path.read_bytes()).hexdigest(), "3313199adea191ad76b5b03c06839dfd473d0677f1e026bc84d77cfa2ce845d9")
        self.assertEqual((dry["channels"], wet["channels"]), (1, 2))
        self.assertGreater(wet["frames"], dry["frames"])
        self.assertEqual(wet["clipped_samples"], 0)
        self.assertLess(wet["end_peak_last_50ms"], 1e-5)
        with wave.open(str(wet_path), "rb") as audio:
            self.assertEqual((audio.getnchannels(), audio.getframerate(), audio.getsampwidth()), (2, 48000, 2))
            self.assertEqual(audio.getnframes(), wet["frames"])
            pcm = struct.unpack("<" + "h" * (audio.getnframes() * 2), audio.readframes(audio.getnframes()))
            self.assertTrue(any(pcm[i] != pcm[i + 1] for i in range(0, len(pcm), 2)))
            self.assertLess(max(map(abs, pcm)), 32767)
        again = self.directory / "repeat.wav"
        report = self.cli("render", self.source, "--production", ROOT / "examples/room.json", "--output", again)
        self.assertEqual(again.read_bytes(), wet_path.read_bytes())
        self.assertEqual(report["render_identity_sha256"], wet["render_identity_sha256"])
        self.assertEqual(self.source.read_bytes(), source_bytes)

    def test_saved_v2_production_matches_override_and_explicit_bypass(self):
        pattern = json.loads(self.source.read_text(encoding="utf-8"))
        pattern.update(schema_version=2, gestures=[], production=self.settings)
        pattern["instrument"]["model"] = "baseline_0_1"
        saved = self.directory / "saved.gtrk.json"
        saved.write_text(json.dumps(pattern), encoding="utf-8")
        wet, override, dry, bypass = [self.directory / name for name in ("wet.wav", "override.wav", "dry.wav", "bypass.wav")]
        wet_report = self.cli("render", saved, "--output", wet)
        self.cli("render", self.source, "--production", ROOT / "examples/room.json", "--output", override)
        self.cli("render", self.source, "--output", dry)
        report = self.cli("render", saved, "--bypass-production", "--output", bypass)
        self.assertEqual(wet.read_bytes(), override.read_bytes())
        self.assertEqual(bypass.read_bytes(), dry.read_bytes())
        self.assertTrue(report["production_bypassed"])
        self.assertFalse(report["production_active"])
        script = self.directory / "preserve.lua"
        script.write_text("gtracker.set_tempo(90)\n", encoding="utf-8")
        reopened = self.directory / "reopened.gtrk.json"
        self.cli("transform", saved, "--script", script, "--output", reopened)
        self.assertEqual(json.loads(reopened.read_text(encoding="utf-8"))["production"], wet_report["production"])

    def test_strict_settings_fail_without_replacing_output(self):
        for changes in ({"version": 2}, {"algorithm": "future"}, {"enabled": 1}, {"wet": True}, {"wet": 1.1}, {"damping": -1}, {"predelay_ms": 201}, {"width": -0.1}, {"output_gain_db": 7}, {"future": 1}):
            with self.subTest(changes=changes):
                settings = deepcopy(self.settings); settings.update(changes)
                path = self.directory / "settings.json"; path.write_text(json.dumps(settings), encoding="utf-8")
                output = self.directory / "untouched.wav"; output.write_bytes(b"previous output")
                report = self.cli("render", self.source, "--production", path, "--output", output, success=False)
                self.assertFalse(report["ok"])
                self.assertEqual(output.read_bytes(), b"previous output")

    def test_dev_wrapper_forwards_override_and_bypass(self):
        output = self.directory / "wrapped.wav"
        process = subprocess.run([sys.executable, "tools/dev.py", "render", str(self.source), "--output", str(output),
                                  "--production", "examples/room.json", "--bypass-production", "--json"], cwd=ROOT, encoding="utf-8", capture_output=True)
        self.assertEqual(process.returncode, 0, process.stderr or process.stdout)
        self.assertTrue(json.loads(process.stdout)["production_bypassed"])
        self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(), "3313199adea191ad76b5b03c06839dfd473d0677f1e026bc84d77cfa2ce845d9")

    def test_render_refuses_to_overwrite_selected_settings_or_script(self):
        settings = self.directory / "settings.json"
        settings.write_text(json.dumps(self.settings), encoding="utf-8")
        before = settings.read_bytes()
        self.cli("render", self.source, "--production", settings, "--output", settings, success=False)
        self.assertEqual(settings.read_bytes(), before)
        script = self.directory / "selected.lua"
        script.write_text("gtracker.set_tempo(90)\n", encoding="utf-8")
        before = script.read_bytes()
        self.cli("render", self.source, "--script", script, "--output", script, success=False)
        self.assertEqual(script.read_bytes(), before)

    def test_export_reports_deliberately_overdriven_output(self):
        pattern = json.loads(self.source.read_text(encoding="utf-8"))
        settings = deepcopy(self.settings); settings.update(wet=0, output_gain_db=6)
        pattern.update(schema_version=2, gestures=[], production=settings, length_beats=0.5)
        pattern["instrument"].update(model="baseline_0_1", body_mix=1)
        pattern["events"] = [{"id": f"hit-{i}", "type": "body_hit", "beat": 0, "strength": 1} for i in range(64)]
        source = self.directory / "overdrive.gtrk.json"; source.write_text(json.dumps(pattern), encoding="utf-8")
        output = self.directory / "overdrive.wav"
        report = self.cli("render", source, "--output", output, "--tail", "0.1")
        self.assertGreater(report["peak"], 1)
        self.assertGreater(report["clipped_samples"], 0)
        self.assertEqual(report["production_tail_seconds"], 0)
        with wave.open(str(output), "rb") as audio:
            pcm = struct.unpack("<" + "h" * (audio.getnframes() * 2), audio.readframes(audio.getnframes()))
        self.assertEqual(max(map(abs, pcm)), 32767)


if __name__ == "__main__": unittest.main()
