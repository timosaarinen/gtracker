"""Native v2 authoring and preserved-source integration; generated audio only."""

from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools import dev, takes

ROOT = Path(__file__).resolve().parents[1]
CLI = dev.executable(ROOT, "headless", "gtracker_cli", require=False)


@unittest.skipUnless(CLI.is_file(), "Build headless before v2 CLI authoring checks")
class GestureCliTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / ".local/tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.pattern = ROOT / "examples/slide-study.gtrk.json"
        self.output = self.directory / "slide-滑音.wav"

    def cli(self, *arguments, code=0):
        result = subprocess.run([str(CLI), *map(str, arguments), "--json"], capture_output=True, encoding="utf-8", timeout=15)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def test_v2_render_reports_model_and_reproduces_saved_control_points(self):
        report = self.cli("render", self.pattern, "--output", self.output)
        self.assertEqual(report["schema_version"], 2)
        self.assertEqual(report["instrument_model"], "contact_0_2")
        self.assertEqual(report["gestures"], 4)
        self.assertEqual(report["frames"], 336000)
        self.assertGreater(report["peak"], 0.001)
        original = self.output.read_bytes()
        self.cli("render", self.pattern, "--output", self.output)
        self.assertEqual(self.output.read_bytes(), original)

    def test_lua_migration_preserves_v1_and_curve_ids_microtiming_through_transposition(self):
        source = ROOT / "examples/campfire.gtrk.json"
        original = source.read_bytes()
        migrated = self.directory / "migrated-移動.gtrk.json"
        report = self.cli("transform", source, "--script", ROOT / "scripts/slide.lua", "--output", migrated)
        self.assertEqual((report["schema_version"], report["instrument_model"], report["gestures"]), (2, "contact_0_2", 1))
        score = takes.read_json(migrated)
        self.assertEqual(score["gestures"][0]["points"][1]["beat"], 0.333333333333)
        self.assertEqual(source.read_bytes(), original)
        script = self.directory / "transpose.lua"
        script.write_text("gtracker.transpose(1)\n", encoding="utf-8")
        transposed = self.directory / "transposed.json"
        self.cli("transform", migrated, "--script", script, "--output", transposed)
        changed = takes.read_json(transposed)
        self.assertEqual(changed["gestures"][0]["id"], score["gestures"][0]["id"])
        self.assertEqual(changed["gestures"][0]["points"][1]["beat"], score["gestures"][0]["points"][1]["beat"])
        self.assertEqual(changed["gestures"][0]["points"][1]["fret"], 3.5)
        self.cli("render", transposed, "--output", self.output)

    def test_unknown_future_data_and_subframe_curves_never_replace_output(self):
        base = takes.read_json(self.pattern)
        self.output.write_bytes(b"previous render")
        changes = [lambda p: p.update(schema_version=3), lambda p: p["instrument"].update(model="future_model"),
                   lambda p: p["gestures"][0].update(type="future_hammer"),
                   lambda p: p["gestures"][0]["points"][0].update(interpolation="future"),
                   lambda p: p.update(schema_version=1)]
        path = self.directory / "invalid.json"
        for change in changes:
            score = deepcopy(base)
            change(score)
            dev.atomic_json(path, score)
            self.cli("render", path, "--output", self.output, code=2)
            self.assertEqual(self.output.read_bytes(), b"previous render")
        base["gestures"][0]["points"][1]["beat"] = 1e-12
        dev.atomic_json(path, base)
        self.cli("validate", path)
        report = self.cli("render", path, "--output", self.output, code=2)
        self.assertIn("closer than one sample frame", report["error"])
        self.assertEqual(self.output.read_bytes(), b"previous render")

    def test_v2_take_retains_source_mapping_and_marks_edited_curve_analysis_old(self):
        self.cli("render", self.pattern, "--output", self.output)
        source = self.output.read_bytes()
        output = self.directory / "take"
        take = takes.import_take(self.output, self.pattern, output, zero_frame=37)
        self.assertEqual(take["alignment"]["pattern_zero_frame"], 37)
        self.assertEqual((output / "pattern.gtrk.json").read_bytes(), self.pattern.read_bytes())
        info = self.cli("take-info", output / "take.json")
        self.assertTrue(info["analysis_matches_pattern_revision"])
        changed = takes.read_json(output / "pattern.gtrk.json")
        changed["revision"] += 1
        changed["gestures"][0]["points"][1]["fret"] = 3.5
        dev.atomic_json(output / "pattern.gtrk.json", changed)
        info = self.cli("take-info", output / "take.json")
        self.assertFalse(info["analysis_matches_pattern_revision"])
        self.assertEqual(info["source_sha256"], take["source"]["sha256"])
        self.assertEqual(takes.inspect_take(output / "take.json")["analysis_status"], "metadata_only")
        self.assertEqual((output / "source.wav").read_bytes(), source)


if __name__ == "__main__":
    unittest.main()
