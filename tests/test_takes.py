"""Preserved-take timing, provenance, source integrity and transactional import."""

from __future__ import annotations

from contextlib import redirect_stdout
from copy import deepcopy
import hashlib
import io
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import wave

from tools import dev, takes

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / ".local/build/headless/bin" / ("gtracker_cli.exe" if os.name == "nt" else "gtracker_cli")


def metadata(rate=48000):
    return {
        "schema": "gtracker.take", "version": 1, "id": "timing-test", "origin": "import",
        "pattern_path": "pattern.gtrk.json", "source_pattern_revision": 1,
        "source": {"path": "source.wav", "name": "original.wav", "sha256": "0" * 64, "size_bytes": 44,
                   "encoding": "integer_pcm", "sample_width_bits": 16, "channels": 1,
                   "sample_rate_hz": rate, "frames": rate * 10, "duration_seconds": 10.0},
        "alignment": {"coordinate_system": "source_samples", "pattern_zero_frame": rate,
                      "input_offset_frames": rate // 100, "offset_status": "manual", "tempo_bpm": 90,
                      "pattern_length_beats": 8, "selection_start_frame": 0, "selection_end_frame": rate * 10},
        "analysis_path": "analysis.json", "created_utc": "2026-10-18T00:00:00Z",
    }


def make_source(path: Path, rate: int, channels: int, width: int):
    samples = []
    scale = (1 << (width * 8 - 1)) - 1
    for frame in range(rate // 10):
        for channel in range(channels):
            sample = round(math.sin(frame * (0.07 + channel * 0.03)) * scale * 0.4)
            samples.append((sample + 128 if width == 1 else sample).to_bytes(width, "little", signed=width != 1))
    with wave.open(str(path), "wb") as output:
        output.setframerate(rate)
        output.setnchannels(channels)
        output.setsampwidth(width)
        output.writeframes(b"".join(samples))


class TakeTimingTests(unittest.TestCase):
    def test_sample_mapping_preserves_offsets_and_subbeat_timing(self):
        for rate in (44100, 48000):
            take = metadata(rate)
            takes.validate_metadata(take, ROOT / ".local/timing-test/take.json")
            zero = rate + rate // 100
            self.assertEqual(takes.source_frame_to_beat(zero, take), 0)
            self.assertEqual(takes.source_frame_to_beat(zero + rate * 2, take), 3)
            self.assertAlmostEqual(takes.source_frame_to_beat(0, take), -1.515)
            self.assertAlmostEqual(takes.source_frame_to_beat(zero + 1, take), 1.5 / rate)

    def test_bad_versions_ranges_types_and_unknown_fields_are_rejected(self):
        changes = [
            ((), "version", 2), ((), "future", 1), (("source",), "frames", True),
            (("source",), "sha256", "not a hash"), (("source",), "duration_seconds", 11),
            (("alignment",), "pattern_zero_frame", 999999999), (("alignment",), "offset_status", "measured"),
            (("alignment",), "selection_end_frame", 0), (("alignment",), "tempo_bpm", float("nan")),
            (("alignment",), "input_offset_frames", False), (("alignment",), "curve", []),
            (("alignment",), "offset_status", "uncalibrated"),
        ]
        for parents, field, value in changes:
            take = metadata()
            target = take
            for parent in parents: target = target[parent]
            target[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                takes.validate_metadata(take, ROOT / ".local/timing-test/take.json")

    def test_references_cannot_leave_the_bundle(self):
        for reference in ("../private.wav", "/private.wav", "C:/private.wav", "audio\\private.wav"):
            with self.subTest(reference=reference), self.assertRaises(ValueError):
                takes.bundle_file(ROOT / ".local/example/take.json", reference)


@unittest.skipUnless(CLI.is_file(), "Native CLI is not built; configure/build headless before take integration tests")
class NativeTakeImportTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / ".local/tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.pattern = ROOT / "examples/campfire.gtrk.json"
        self.input = self.directory / "alkuperäinen-原音.wav"
        self.output = self.directory / "take-音"
        make_source(self.input, 48000, 1, 2)

    def test_import_preserves_all_pcm_widths_rates_channels_and_pattern_bytes(self):
        for index, (rate, channels, width) in enumerate([(44100, 1, 2), (48000, 2, 3), (44100, 2, 4), (48000, 1, 1)]):
            with self.subTest(rate=rate, channels=channels, width=width):
                make_source(self.input, rate, channels, width)
                original = self.input.read_bytes()
                output = self.directory / str(index)
                take = takes.import_take(self.input, self.pattern, output, zero_frame=rate // 100)
                self.assertEqual((output / "source.wav").read_bytes(), original)
                self.assertEqual(self.input.read_bytes(), original)
                self.assertEqual((output / "pattern.gtrk.json").read_bytes(), self.pattern.read_bytes())
                self.assertEqual(take["source"]["sha256"], hashlib.sha256(original).hexdigest())
                self.assertEqual((take["source"]["sample_rate_hz"], take["source"]["channels"], take["source"]["sample_width_bits"]),
                                 (rate, channels, width * 8))
                report = takes.inspect_take(output / "take.json")
                self.assertEqual(report["analysis_status"], "metadata_only")
                self.assertFalse(report["capabilities"]["note_transcription"])
                self.assertTrue(report["analysis_matches_pattern_revision"])
                native = self.native_info(output / "take.json")
                self.assertEqual(native["source_sha256"], take["source"]["sha256"])
                self.assertEqual((native["sample_rate"], native["channels"], native["sample_width_bits"]), (rate, channels, width * 8))
                self.assertAlmostEqual(native["selection_start_beat"], report["selection_start_beat"])
                self.assertAlmostEqual(native["selection_end_beat"], report["selection_end_beat"])
                self.assertAlmostEqual(native["peak"], report["diagnostics"]["peak"], places=7)
                self.assertAlmostEqual(native["rms"], report["diagnostics"]["rms"], places=7)

    def native_info(self, path, success=True):
        result = subprocess.run([str(CLI), "take-info", str(path), "--json"], capture_output=True, encoding="utf-8", check=False)
        self.assertEqual(result.returncode, 0 if success else 2, result.stdout + result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["ok"], success)
        return report

    def test_moved_bundle_reopens_and_revised_pattern_is_explicitly_stale(self):
        takes.import_take(self.input, self.pattern, self.output)
        moved = self.directory / "moved"
        self.assertTrue(self.output.resolve().is_relative_to(self.directory.resolve()))
        self.assertTrue(moved.resolve().is_relative_to(self.directory.resolve()))
        self.output.rename(moved)
        report = takes.inspect_take(moved / "take.json")
        self.assertTrue(report["analysis_matches_pattern_revision"])
        pattern = json.loads((moved / "pattern.gtrk.json").read_text(encoding="utf-8"))
        events = deepcopy(pattern["events"])
        pattern["revision"] += 1
        dev.atomic_json(moved / "pattern.gtrk.json", pattern)
        self.assertFalse(takes.inspect_take(moved / "take.json")["analysis_matches_pattern_revision"])
        self.assertFalse(self.native_info(moved / "take.json")["analysis_matches_pattern_revision"])
        self.assertEqual(pattern["events"], events, "metadata analysis must not clear or replace authored notes")

    def test_existing_output_invalid_selection_and_cancel_leave_user_data_intact(self):
        original = self.input.read_bytes()
        with self.assertRaises(ValueError):
            takes.import_take(self.input, self.pattern, self.output, selection_end=0)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.directory.glob(".*.import-*")))
        with mock.patch.object(takes.analyze, "analyze_file", side_effect=KeyboardInterrupt):
            output = io.StringIO()
            with redirect_stdout(output):
                code = takes.main(["import", str(self.input), "--pattern", str(self.pattern), "--output", str(self.output), "--json"])
            self.assertEqual(code, 130)
            self.assertFalse(json.loads(output.getvalue())["ok"])
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.directory.glob(".*.import-*")))
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_text("user work", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "already exists"):
            takes.import_take(self.input, self.pattern, self.output)
        self.assertEqual(marker.read_text(), "user work")
        self.assertEqual(self.input.read_bytes(), original)

    def test_copy_mismatch_and_tampered_sources_or_jobs_are_rejected(self):
        copy = shutil.copyfile
        def changed_copy(source, destination):
            result = copy(source, destination)
            if Path(source) == self.input:
                Path(destination).write_bytes(Path(destination).read_bytes() + b"changed")
            return result
        with mock.patch.object(takes.shutil, "copyfile", side_effect=changed_copy):
            with self.assertRaisesRegex(ValueError, "changed"):
                takes.import_take(self.input, self.pattern, self.output)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.directory.glob(".*.import-*")))
        takes.import_take(self.input, self.pattern, self.output)
        analysis = json.loads((self.output / "analysis.json").read_text(encoding="utf-8"))
        analysis["job_id"] = "wrong-job"
        dev.atomic_json(self.output / "analysis.json", analysis)
        with self.assertRaisesRegex(ValueError, "job/source/revision"):
            takes.inspect_take(self.output / "take.json")
        self.native_info(self.output / "take.json", success=False)
        dev.atomic_json(self.output / "analysis.json", [])
        with self.assertRaises(ValueError):
            takes.inspect_take(self.output / "take.json")
        # Restore the original analysis, then change the preserved audio only.
        analysis["job_id"] = json.loads((self.output / "take.json").read_text())["id"]
        dev.atomic_json(self.output / "analysis.json", analysis)
        with (self.output / "source.wav").open("ab") as output: output.write(b"changed")
        with self.assertRaisesRegex(ValueError, "no longer matches"):
            takes.inspect_take(self.output / "take.json")
        self.assertIn("SHA-256", self.native_info(self.output / "take.json", success=False)["error"])

    def test_developer_cli_is_json_and_works_from_another_working_directory(self):
        command = [sys.executable, str(ROOT / "tools/dev.py"), "import-take", str(self.input),
                   "--pattern", str(self.pattern), "--output", str(self.output), "--zero-frame", "100",
                   "--input-offset-frames", "24", "--json"]
        result = subprocess.run(command, cwd=self.directory, capture_output=True, encoding="utf-8", check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(json.loads(result.stdout)["ok"])
        result = subprocess.run([sys.executable, str(ROOT / "tools/dev.py"), "take-info", str(self.output / "take.json"), "--json"],
                                cwd=self.directory, capture_output=True, encoding="utf-8", check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["alignment"]["input_offset_frames"], 24)
        self.assertEqual(report["alignment"]["offset_status"], "manual")

    def test_native_reader_rejects_bad_versions_fields_and_frame_mapping(self):
        take = takes.import_take(self.input, self.pattern, self.output)
        for parents, field, value in [
            ((), "version", True), ((), "version", 2), ((), "future", []),
            (("source",), "path", "../private.wav"), (("source",), "frames", True),
            (("alignment",), "pattern_zero_frame", 999999999),
            (("alignment",), "input_offset_frames", -9223372036854775808),
            (("alignment",), "input_offset_frames", 18446744073709551615),
            (("alignment",), "selection_end_frame", 0), (("alignment",), "offset_status", "measured"),
        ]:
            changed = deepcopy(take)
            target = changed
            for parent in parents: target = target[parent]
            target[field] = value
            dev.atomic_json(self.output / "take.json", changed)
            with self.subTest(field=field, value=value):
                self.native_info(self.output / "take.json", success=False)


if __name__ == "__main__":
    unittest.main()
