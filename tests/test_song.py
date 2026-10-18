"""Continuous song CLI, portable references/snapshots, variants and interruption."""
from copy import deepcopy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from tools import dev

ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("GTRACKER_TEST_CLI", dev.executable(ROOT, "headless", "gtracker_cli")))


@unittest.skipUnless(CLI.is_file(), "Native CLI not built")
class SongTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / ".local/tests"; directory.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=directory)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.bundle = self.directory / "sävel-音楽"; self.bundle.mkdir()
        for name in ("night-shift.gsong.json", "slide-study.gtrk.json", "night-bridge.gtrk.json"):
            shutil.copyfile(ROOT / "examples" / name, self.bundle / name)
        self.source = self.bundle / "night-shift.gsong.json"
        self.output = self.directory / "audio.wav"
        self.song = json.loads(self.source.read_text(encoding="utf-8"))

    def cli(self, command="render-song", *flags, code=0, source=None, output=None):
        arguments = [str(CLI), command, str(source or self.source)]
        if command != "validate-song": arguments += ["--output", str(output or self.output)]
        result = subprocess.run([*arguments, *map(str, flags), "--json"], cwd=self.directory, encoding="utf-8", capture_output=True, timeout=20)
        self.assertEqual(result.returncode, code, result.stderr or result.stdout)
        return json.loads(result.stdout), result

    def save(self, song=None):
        self.source.write_text(json.dumps(song or self.song), encoding="utf-8")

    def test_song_reference_bundle_moves_and_snapshot_reproduces_without_source_files(self):
        source_bytes = {p.name: p.read_bytes() for p in self.bundle.iterdir()}
        full, _ = self.cli()
        reference = self.output.read_bytes()
        self.assertEqual((full["patterns"], full["instances"], full["length_beats"]), (2, 8, 68))
        self.assertEqual(full["channels"], 2)
        self.assertEqual(full["clipped_samples"], 0)
        self.assertLess(full["end_peak_last_50ms"], 1e-5)
        self.assertEqual(full["audio_buffer_bytes"], 4096 * 2 * 4)
        self.assertGreater(full["pcm_bytes"], full["source_snapshot_utf8_bytes"] * 100)
        self.assertEqual({p.name: p.read_bytes() for p in self.bundle.iterdir()}, source_bytes)
        snapshot = self.directory / "snapshot.gsong.json"
        self.cli("snapshot-song", output=snapshot)
        stored = json.loads(snapshot.read_text(encoding="utf-8"))
        self.assertTrue(all("pattern" in p and "file" not in p for p in stored["patterns"]))
        moved = self.directory / "relocated-ö"; self.bundle.rename(moved)
        report, _ = self.cli(source=moved / self.source.name)
        self.assertEqual(report["render_identity_sha256"], full["render_identity_sha256"])
        self.assertEqual(self.output.read_bytes(), reference)
        for path in moved.iterdir(): path.unlink()  # Owned generated text fixture only.
        report, _ = self.cli(source=snapshot)
        self.assertEqual(report["render_identity_sha256"], full["render_identity_sha256"])
        self.assertEqual(self.output.read_bytes(), reference)

    def test_full_song_preview_warms_contacts_and_room_without_appending_tail(self):
        full, _ = self.cli()
        reference = self.output.read_bytes()
        crop, progress = self.cli("render-song", "--start-frame", 400001, "--end-frame", 500003, "--block-frames", 311, "--progress-json")
        self.assertEqual(self.output.read_bytes()[44:], reference[44+400001*4:44+500003*4])
        self.assertEqual((crop["frames"], crop["warmup_frames"]), (100002, 400001))
        self.assertFalse(crop["output_reaches_timeline_end"])
        self.assertNotEqual(crop["render_identity_sha256"], full["render_identity_sha256"])
        self.assertEqual(json.loads(progress.stderr.splitlines()[-1])["written_frames"], 100002)

    def test_variation_and_referenced_pattern_edits_change_output_and_identity(self):
        first, _ = self.cli()
        original = self.output.read_bytes()
        self.song["order"][1]["transpose"] = 1.25; self.save()
        varied, _ = self.cli()
        self.assertNotEqual(varied["render_identity_sha256"], first["render_identity_sha256"])
        self.assertNotEqual(self.output.read_bytes(), original)
        pattern = self.bundle / "night-bridge.gtrk.json"
        score = json.loads(pattern.read_text(encoding="utf-8")); score["events"][0]["strength"] *= 0.75
        pattern.write_text(json.dumps(score), encoding="utf-8")
        edited, _ = self.cli()
        self.assertNotEqual(edited["render_identity_sha256"], varied["render_identity_sha256"])

    def test_source_patterns_and_song_cannot_be_overwritten_by_wav_or_snapshot(self):
        sources = list(self.bundle.iterdir())
        original = {p: p.read_bytes() for p in sources}
        for path in sources:
            for command in ("render-song", "snapshot-song"):
                with self.subTest(path=path.name, command=command): self.cli(command, output=path, code=2)
        self.assertEqual({p: p.read_bytes() for p in sources}, original)

    def test_strict_schema_rejects_future_fields_bad_repeats_and_ambiguous_sources(self):
        mutations = [lambda s: s.update(version=2), lambda s: s.update(future=True), lambda s: s.update(seed=-1),
                     lambda s: s.update(tempo_bpm=True), lambda s: s.update(instrument_pattern="missing"),
                     lambda s: s["patterns"].append(deepcopy(s["patterns"][0])),
                     lambda s: s["patterns"][0].update(pattern={}), lambda s: s["patterns"][0].update(file="C:/absolute.json"),
                     lambda s: s["order"][0].update(repeat=0), lambda s: s["order"][0].update(repeat=True),
                     lambda s: s["order"][0].update(time_scale=0), lambda s: s["order"][0].update(transpose=-24),
                     lambda s: s["order"][0].update(strength_scale=2), lambda s: s["order"][0].update(pattern="missing"),
                     lambda s: s["order"][0].update(gap_beats=-1), lambda s: s["order"][0].update(unknown=1)]
        for mutate in mutations:
            song = deepcopy(self.song); mutate(song); self.save(song)
            self.output.write_bytes(b"previous completed audio")
            self.cli(code=2)
            self.assertEqual(self.output.read_bytes(), b"previous completed audio")

    def test_snapshot_preserves_curves_before_selecting_a_sufficient_sample_rate(self):
        source = self.bundle / "slide-study.gtrk.json"
        score = json.loads(source.read_text(encoding="utf-8"))
        score["gestures"][0]["points"] = [{"beat": 0, "fret": 0}, {"beat": 0.00001, "fret": 2}]
        source.write_text(json.dumps(score), encoding="utf-8")
        self.cli("snapshot-song", output=self.directory / "tiny-curve.gsong.json")
        self.cli("validate-song", code=2)
        report, _ = self.cli("validate-song", "--sample-rate", 192000)
        self.assertEqual(report["sample_rate"], 192000)

    def test_cancelled_long_song_keeps_old_output_beyond_in_memory_audio_limit(self):
        phrase = json.loads((ROOT / "examples/campfire.gtrk.json").read_text(encoding="utf-8"))
        phrase["length_beats"] = 256
        song = deepcopy(self.song); song.pop("production")
        song.update(tempo_bpm=20, instrument_pattern="long", patterns=[{"id": "long", "pattern": phrase}],
                    order=[{"id":"repeat", "pattern":"long", "repeat":2, "time_scale":1, "transpose":0, "strength_scale":1, "gap_beats":0}])
        self.save(song); self.output.write_bytes(b"previous completed audio")
        marker = self.directory / "cancel"
        process = subprocess.Popen([str(CLI), "render-song", str(self.source), "--output", str(self.output), "--progress-json", "--cancel-file", str(marker), "--json"],
                                   cwd=ROOT, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            self.assertGreater(json.loads(process.stderr.readline())["processing_frames"], 32*1024*1024)
            marker.touch(); stdout, stderr = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 130, stdout + stderr)
            self.assertEqual(json.loads(stdout)["status"], "cancelled")
            self.assertEqual(self.output.read_bytes(), b"previous completed audio")
        finally:
            if process.poll() is None: process.kill(); process.communicate()
        self.assertFalse(list(self.directory.glob("*.tmp-*")))

    def test_developer_wrapper_preserves_song_json_and_progress_streams(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/dev.py"), "render-song", str(self.source), "--output", str(self.output),
                                 "--start-frame", "10001", "--end-frame", "20003", "--progress-json", "--json"],
                                cwd=self.directory, encoding="utf-8", capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr or result.stdout)
        self.assertEqual(json.loads(result.stdout)["frames"], 10002)
        self.assertEqual(json.loads(result.stderr.splitlines()[-1])["written_frames"], 10002)


if __name__ == "__main__": unittest.main()
