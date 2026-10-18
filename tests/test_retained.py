"""Explicit retained-model authoring through existing native document/CLI paths."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools import dev

ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("GTRACKER_TEST_CLI", dev.executable(ROOT, "headless", "gtracker_cli")))


@unittest.skipUnless(CLI.is_file(), "Native CLI not built")
class RetainedCliTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / ".local/tests"; directory.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=directory)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def cli(self, *arguments):
        result = subprocess.run([str(CLI), *map(str, arguments), "--json"], cwd=ROOT, capture_output=True, encoding="utf-8", check=True)
        return json.loads(result.stdout)

    def test_explicit_lua_model_choice_roundtrips_and_changes_replucks_only(self):
        source = ROOT / "examples/repluck-study.gtrk.json"
        original = source.read_bytes()
        saved = self.directory / "retained.gtrk.json"
        self.cli("transform", source, "--script", ROOT / "scripts/retain.lua", "--output", saved)
        # Match native float serialization before comparing the musical data.
        empty_script = self.directory / "identity.lua"
        empty_script.write_text("-- Keep the musical document unchanged.\n", encoding="utf-8")
        canonical = self.directory / "canonical.gtrk.json"
        self.cli("transform", source, "--script", empty_script, "--output", canonical)
        before, after = json.loads(canonical.read_text(encoding="utf-8")), json.loads(saved.read_text(encoding="utf-8"))
        self.assertEqual(after["instrument"]["model"], "retained_0_5")
        self.assertEqual(after["events"], before["events"])
        self.assertEqual(after["gestures"], before["gestures"])
        self.assertEqual(source.read_bytes(), original)
        legacy, retained, repeated = [self.directory / name for name in ("legacy.wav", "retained.wav", "repeat.wav")]
        self.cli("render", source, "--output", legacy)
        report = self.cli("render", saved, "--output", retained)
        self.cli("render", saved, "--output", repeated)
        self.assertEqual(report["instrument_model"], "retained_0_5")
        self.assertEqual(report["clipped_samples"], 0)
        self.assertNotEqual(retained.read_bytes(), legacy.read_bytes())
        self.assertEqual(repeated.read_bytes(), retained.read_bytes())

    def test_retained_slides_survive_add_save_and_native_render(self):
        source = ROOT / "examples/slide-study.gtrk.json"
        script = self.directory / "select.lua"
        script.write_text('gtracker.set_model("retained_0_5")\ngtracker.add_slide(7.5, 1, {{beat=0,fret=0},{beat=0.25,fret=2}})\n', encoding="utf-8")
        saved = self.directory / "retained.gtrk.json"
        transformed = self.cli("transform", source, "--script", script, "--output", saved)
        self.assertEqual((transformed["instrument_model"], transformed["gestures"]), ("retained_0_5", 5))
        report = self.cli("render", saved, "--output", self.directory / "slide.wav")
        self.assertGreater(report["peak"], 0)
        self.assertEqual(report["clipped_samples"], 0)


if __name__ == "__main__": unittest.main()
