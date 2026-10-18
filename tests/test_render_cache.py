"""Persistent local cache validity, exact previews and conservative invalidation."""
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools import dev

ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get("GTRACKER_TEST_CLI", dev.executable(ROOT, "headless", "gtracker_cli")))


@unittest.skipUnless(CLI.is_file(), "Native CLI not built")
class RenderCacheTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / ".local/tests"; directory.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=directory)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source = self.directory / "song.gsong.json"
        snapshot = subprocess.run([str(CLI), "snapshot-song", str(ROOT / "examples/night-shift.gsong.json"), "--output", str(self.source), "--json"],
                                  cwd=ROOT, capture_output=True, encoding="utf-8", check=True)
        self.assertTrue(json.loads(snapshot.stdout)["ok"])
        self.song = json.loads(self.source.read_text(encoding="utf-8"))
        self.cache = self.directory / "välimuisti-音楽"
        self.output = self.directory / "output.wav"

    def cli(self, *flags, source=None, cache=None, code=0):
        arguments = [str(CLI), "render-song", str(source or self.source), "--output", str(self.output), "--cache-dir", str(cache or self.cache), *map(str, flags), "--json"]
        result = subprocess.run(arguments, cwd=self.directory, encoding="utf-8", capture_output=True, timeout=20)
        self.assertEqual(result.returncode, code, result.stderr or result.stdout)
        return json.loads(result.stdout), result

    def paths(self, report):
        key = report["cache_key"]
        base = self.cache / "gtracker-v1" / key[:2] / key
        return base.with_suffix(".wav"), base.with_suffix(".json")

    def test_hit_skips_generation_preserves_bytes_and_reports_current_build(self):
        first, _ = self.cli()
        expected = self.output.read_bytes()
        second, _ = self.cli("--block-frames", 127)
        self.assertEqual((first["cache_status"], second["cache_status"]), ("miss", "hit"))
        self.assertEqual(self.output.read_bytes(), expected)
        self.assertEqual((second["render_ms"], second["audio_buffer_bytes"], second["warmup_frames"]), (0, 0, 0))
        self.assertEqual(second["source_render_ms"], first["render_ms"])
        self.assertEqual(second["signal_metrics_origin"], "checked_cached_float_receipt")
        for key in ("peak", "rms", "clipped_samples", "end_peak_last_50ms", "render_identity_sha256"): self.assertEqual(second[key], first[key])
        version = json.loads(subprocess.run([str(CLI), "--version", "--json"], encoding="utf-8", capture_output=True, check=True).stdout)
        self.assertEqual(version["renderer_build_id"], first["renderer_build_id"])
        self.assertRegex(version["renderer_build_id"], r"^[0-9a-f]{64}/.+$")
        wav, receipt = self.paths(first)
        self.assertEqual(json.loads(receipt.read_text())["payload"]["wav_sha256"], hashlib.sha256(wav.read_bytes()).hexdigest())

    def test_every_audible_input_is_in_identity_even_for_a_short_preview(self):
        flags = ("--end-frame", 12000)
        initial, _ = self.cli(*flags)
        mutations = [lambda s: s.update(seed=s["seed"]+1), lambda s: s.update(tempo_bpm=s["tempo_bpm"]+1),
                     lambda s: s["production"].update(enabled=False), lambda s: s["production"].update(wet=0.2),
                     lambda s: s["production"].update(decay_seconds=1.3), lambda s: s["production"].update(damping=0.4),
                     lambda s: s["production"].update(predelay_ms=19), lambda s: s["production"].update(width=0.8),
                     lambda s: s["production"].update(output_gain_db=-1), lambda s: s["order"][0].update(transpose=0.25),
                     lambda s: s["order"][0].update(time_scale=1.1), lambda s: s["order"][0].update(strength_scale=0.6),
                     lambda s: s["order"][0].update(gap_beats=0.1), lambda s: s["order"][0].update(repeat=2),
                     lambda s: s["patterns"][0]["pattern"]["events"][0].update(strength=0.7),
                     lambda s: s["patterns"][0]["pattern"]["events"][0].update(beat=0.01),
                     lambda s: s["patterns"][0]["pattern"]["gestures"][0]["points"][1].update(fret=2.5)]
        for mutate in mutations:
            song = deepcopy(self.song); mutate(song); self.source.write_text(json.dumps(song), encoding="utf-8")
            changed, _ = self.cli(*flags)
            self.assertEqual(changed["cache_status"], "miss")
            self.assertNotEqual(changed["cache_key"], initial["cache_key"])
        for field, value in (("brightness", 0.75), ("decay_seconds", 3.5), ("body_mix", 0.1), ("tuning_midi", [64, 59, 55, 50, 45, 40.25])):
            song = deepcopy(self.song)
            for pattern in song["patterns"]: pattern["pattern"]["instrument"][field] = value
            self.source.write_text(json.dumps(song), encoding="utf-8")
            changed, _ = self.cli(*flags)
            self.assertNotEqual(changed["cache_key"], initial["cache_key"])
        self.source.write_text(json.dumps(self.song), encoding="utf-8")
        for extra in (("--sample-rate", 44100), ("--tail", 1), ("--bypass-production",), ("--start-frame", 1)):
            changed, _ = self.cli(*flags, *extra)
            self.assertNotEqual(changed["cache_key"], initial["cache_key"])
        reused, _ = self.cli(*flags, "--block-frames", 65536)
        self.assertEqual((reused["cache_status"], reused["cache_key"]), ("hit", initial["cache_key"]))

    def test_corrupt_incomplete_and_stale_build_entries_recover(self):
        first, _ = self.cli("--end-frame", 12000)
        expected = self.output.read_bytes()
        wav, receipt = self.paths(first)
        mutations = [lambda: wav.write_bytes(b"truncated"), lambda: receipt.write_text("{broken", encoding="utf-8"),
                     lambda: receipt.unlink(), lambda: wav.unlink(), lambda: receipt.write_text("["*1000+"0"+"]"*1000, encoding="utf-8")]
        for mutate in mutations:
            mutate(); report, _ = self.cli("--end-frame", 12000)
            self.assertEqual(report["cache_status"], "rebuilt")
            self.assertEqual(self.output.read_bytes(), expected)
        for field, value in (("renderer_build_id", "older-source/Release"), ("version", 2), ("frames", 1), ("channels", 1), ("peak", 2), ("identity", "0"*64)):
            stored = json.loads(receipt.read_text(encoding="utf-8")); stored["payload"][field] = value
            # Integrity is not merely the receipt's own checksum: native checks
            # also bind declared build, key, timing and PCM format/content.
            stored["sha256"] = hashlib.sha256(json.dumps(stored["payload"], sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()
            receipt.write_text(json.dumps(stored), encoding="utf-8")
            report, _ = self.cli("--end-frame", 12000)
            self.assertEqual(report["cache_status"], "rebuilt")
            self.assertEqual(self.output.read_bytes(), expected)

    def test_read_only_and_unavailable_cache_do_not_block_completed_audio(self):
        report, _ = self.cli("--end-frame", 12000, "--cache-read-only")
        self.assertEqual(report["cache_status"], "read_only_miss")
        self.assertFalse(self.cache.exists())
        expected = self.output.read_bytes()
        first, _ = self.cli("--end-frame", 12000)
        wav, receipt = self.paths(first); original = (wav.read_bytes(), receipt.read_bytes())
        report, _ = self.cli("--end-frame", 12000, "--cache-read-only")
        self.assertEqual(report["cache_status"], "hit")
        self.assertEqual((wav.read_bytes(), receipt.read_bytes()), original)
        receipt.write_text("corrupt", encoding="utf-8")
        report, _ = self.cli("--end-frame", 12000, "--cache-read-only")
        self.assertEqual(report["cache_status"], "read_only_invalid")
        self.assertEqual(receipt.read_text(), "corrupt")
        blocked = self.directory / "lukittu-音楽"; blocked.write_text("unrelated text", encoding="utf-8")
        report, _ = self.cli("--end-frame", 12000, cache=blocked)
        self.assertEqual(report["cache_status"], "store_failed")
        self.assertEqual(self.output.read_bytes(), expected)
        self.assertEqual(blocked.read_text(encoding="utf-8"), "unrelated text")

    def test_preview_cache_reuses_exact_crop_without_claiming_warmup(self):
        first, _ = self.cli("--start-frame", 400001, "--end-frame", 500003)
        expected = self.output.read_bytes()
        hit, _ = self.cli("--start-frame", 400001, "--end-frame", 500003)
        self.assertEqual(hit["cache_status"], "hit")
        self.assertEqual((hit["crop_start_frame"], hit["warmup_frames"], hit["frames"]), (400001, 0, 100002))
        self.assertFalse(hit["output_reaches_timeline_end"])
        self.assertEqual(self.output.read_bytes(), expected)

    def test_cancelled_cache_validation_retains_old_output_and_valid_entry(self):
        first, _ = self.cli()
        wav, receipt = self.paths(first); original = (wav.read_bytes(), receipt.read_bytes())
        self.output.write_bytes(b"previous completed audio")
        marker = self.directory / "cancel"
        process = subprocess.Popen([str(CLI), "render-song", str(self.source), "--output", str(self.output), "--cache-dir", str(self.cache),
                                    "--cancel-file", str(marker), "--progress-json", "--json"], cwd=ROOT, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            self.assertEqual(json.loads(process.stderr.readline())["phase"], "lookup_hash")
            marker.touch(); stdout, stderr = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 130, stdout + stderr)
            self.assertEqual(json.loads(stdout)["status"], "cancelled")
            self.assertEqual(self.output.read_bytes(), b"previous completed audio")
            self.assertEqual((wav.read_bytes(), receipt.read_bytes()), original)
        finally:
            if process.poll() is None: process.kill(); process.communicate()
        self.assertFalse(any(".tmp-" in p.name or ".cache-stage-" in p.name for p in self.directory.rglob("*")))

    def test_wrapper_preserves_cache_progress_and_no_directory_flags_fail(self):
        args = [sys.executable, str(ROOT / "tools/dev.py"), "render-song", str(self.source), "--output", str(self.output), "--cache-dir", str(self.cache), "--end-frame", "12000", "--progress-json", "--json"]
        for status in ("miss", "hit"):
            result = subprocess.run(args, cwd=self.directory, encoding="utf-8", capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr or result.stdout)
            self.assertEqual(json.loads(result.stdout)["cache_status"], status)
            self.assertTrue(any(json.loads(line)["type"] == "cache_progress" for line in result.stderr.splitlines()))
        invalid = subprocess.run([str(CLI), "render-song", str(self.source), "--output", str(self.output), "--refresh-cache", "--json"], encoding="utf-8", capture_output=True)
        self.assertEqual(invalid.returncode, 2)
        self.cli("--refresh-cache", "--cache-read-only", code=2)


if __name__ == "__main__": unittest.main()
