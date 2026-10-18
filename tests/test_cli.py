"""Native CLI integration. Skipped explicitly when running Python without a build."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import wave


ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("GTRACKER_TEST_CLI", ROOT / ".local/build/headless/bin" /
                          ("gtracker_cli.exe" if os.name == "nt" else "gtracker_cli")))


@unittest.skipUnless(CLI.is_file(), "Native CLI is not built; configure/build the headless profile first")
class NativeCliTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / ".local/tests"
        directory.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=directory)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name) / "yö 音楽"
        self.directory.mkdir()
        self.pattern = self.directory / "sävel-原曲.gtrk.json"
        shutil.copyfile(ROOT / "examples/campfire.gtrk.json", self.pattern)

    def run_cli(self, *arguments: str | Path, success: bool = True) -> dict:
        result = subprocess.run([str(CLI), *map(str, arguments), "--json"], cwd=self.directory,
                                capture_output=True, encoding="utf-8", check=False)
        self.assertEqual(result.returncode, 0 if success else 2, result.stderr or result.stdout)
        report = json.loads(result.stdout)
        self.assertEqual(report["ok"], success)
        return report

    def test_unicode_validate_render_and_transform(self):
        report = self.run_cli("validate", self.pattern)
        self.assertEqual(report["input"], str(self.pattern))
        script = self.directory / "pehmennä-柔らかく.lua"
        script.write_text("\ufeff#!/usr/bin/env lua\ngtracker.set_tempo(120)\n", encoding="utf-8")
        transformed = self.directory / "muokattu-変更.gtrk.json"
        report = self.run_cli("transform", self.pattern, "--script", script, "--output", transformed)
        self.assertEqual(report["output"], str(transformed))
        self.assertEqual(json.loads(transformed.read_text(encoding="utf-8"))["tempo_bpm"], 120)
        output = self.directory / "ääni-音.wav"
        report = self.run_cli("render", transformed, "--output", output, "--sample-rate", "44100", "--tail", "0.5")
        self.assertEqual(report["output"], str(output))
        with wave.open(str(output), "rb") as rendered:
            self.assertEqual((rendered.getframerate(), rendered.getnchannels(), rendered.getsampwidth()),
                             (44100, 1, 2))
            self.assertEqual(rendered.getnframes(), 198450)

    def test_unicode_script_failure_does_not_replace_output(self):
        output = self.directory / "säilytä-保持.json"
        output.write_text("original contents", encoding="utf-8")
        script = self.directory / "virhe-失敗.lua"
        script.write_text("\ufeff#!/usr/bin/env lua\ngtracker.set_tempo(120)\nerror('expected failure')\n", encoding="utf-8")
        report = self.run_cli("transform", self.pattern, "--script", script, "--output", output, success=False)
        self.assertIn("expected failure", report["error"])
        self.assertIn(script.name + ":3:", report["error"])
        self.assertEqual(output.read_text(encoding="utf-8"), "original contents")

    def test_render_rejects_pattern_as_output_and_bad_options(self):
        original = self.pattern.read_bytes()
        report = self.run_cli("render", self.pattern, "--output", self.pattern, success=False)
        self.assertIn("must differ", report["error"])
        self.assertEqual(self.pattern.read_bytes(), original)
        for arguments in [("--tail", "nan"), ("--sample-rate", "44100.5"), ("--surprise", "1")]:
            output = self.directory / "should-not-exist.wav"
            self.run_cli("render", self.pattern, "--output", output, *arguments, success=False)
            self.assertFalse(output.exists())

    def test_errors_remain_json_for_missing_paths_and_non_utf8_lua_errors(self):
        missing = self.directory / "puuttuu-不存在.json"
        report = self.run_cli("validate", missing, success=False)
        self.assertIn(missing.name, report["error"])
        script = self.directory / "raw-error.lua"
        script.write_text("error(string.char(255))\n", encoding="utf-8")
        report = self.run_cli("transform", self.pattern, "--script", script,
                              "--output", self.directory / "unused.json", success=False)
        self.assertIn("Lua:", report["error"])


if __name__ == "__main__":
    unittest.main()
