"""Contract checks for analysis provenance and truthful local verification."""

from __future__ import annotations

from contextlib import redirect_stdout
from datetime import datetime, timedelta, timezone
import io
import json
import math
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import wave


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from analysis import analyze
from tools import dev, repo_guard


def make_wav(path: Path, samples: list[int], width: int = 2, channels: int = 1, rate: int = 8000) -> None:
    data = b"".join(value.to_bytes(width, "little", signed=width != 1) for value in samples)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(width)
        output.setframerate(rate)
        output.writeframes(data)


class AnalysisContractTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def test_mono_metrics_and_unavailable_transcript_are_explicit(self):
        path = self.directory / "take.wav"
        make_wav(path, [0, 16384, -16384, 0])
        result = analyze.analyze_file(path, "take-1", 7, 90)
        self.assertEqual(result["schema"], "gtracker.analysis-result")
        self.assertEqual(result["job_id"], "take-1")
        self.assertEqual(result["source_pattern_revision"], 7)
        self.assertEqual(result["status"], "metadata_only")
        self.assertEqual(result["events"], [])
        self.assertFalse(result["capabilities"]["note_transcription"])
        self.assertFalse(result["capabilities"]["technique_recognition"])
        self.assertAlmostEqual(result["diagnostics"]["peak"], 0.5)
        self.assertAlmostEqual(result["diagnostics"]["rms"], math.sqrt(0.125))
        self.assertEqual(result["source"]["frames"], 4)
        self.assertEqual(result["source"]["duration_seconds"], 4 / 8000)
        self.assertEqual(len(result["source"]["sha256"]), 64)

    def test_stereo_24_bit_sign_and_channel_statistics(self):
        path = self.directory / "stereo.wav"
        make_wav(path, [-8388608, 0, 4194304, 8388607], width=3, channels=2)
        result = analyze.analyze_file(path, "stereo", 0, 100)
        self.assertEqual(result["source"]["frames"], 2)
        self.assertEqual(result["source"]["channels"], 2)
        self.assertEqual(result["source"]["sample_width_bits"], 24)
        self.assertEqual(result["diagnostics"]["channels"][0]["peak"], 1)
        self.assertAlmostEqual(result["diagnostics"]["channels"][0]["rms"], math.sqrt(0.625))
        self.assertAlmostEqual(result["diagnostics"]["channels"][1]["peak"], 8388607 / 8388608)

    def test_unsigned_8_bit_and_signed_32_bit(self):
        path = self.directory / "pcm.wav"
        make_wav(path, [0, 128, 192], width=1)
        result = analyze.analyze_file(path, "pcm8", 1, 80)
        self.assertEqual(result["diagnostics"]["peak"], 1)
        self.assertAlmostEqual(result["diagnostics"]["rms"], math.sqrt(1.25 / 3))
        make_wav(path, [-2147483648, 1073741824], width=4)
        result = analyze.analyze_file(path, "pcm32", 1, 80)
        self.assertEqual(result["diagnostics"]["peak"], 1)
        self.assertAlmostEqual(result["diagnostics"]["rms"], math.sqrt(0.625))

    def test_truncated_wav_and_nonfinite_tempo_fail(self):
        path = self.directory / "take.wav"
        make_wav(path, [0, 100, 200])
        with self.assertRaisesRegex(ValueError, "finite"):
            analyze.analyze_file(path, "bad-tempo", 1, float("nan"))
        path.write_bytes(path.read_bytes()[:-2])
        with self.assertRaisesRegex(ValueError, "truncated"):
            analyze.analyze_file(path, "truncated", 1, 100)

    def test_cli_writes_result_and_preserves_audio(self):
        path = self.directory / "original.wav"
        output = self.directory / "nested" / "result.json"
        make_wav(path, [0, 1000, -1000, 0])
        original = path.read_bytes()
        with redirect_stdout(io.StringIO()):
            code = analyze.main(["--input", str(path), "--output", str(output), "--job-id", "roundtrip", "--pattern-revision", "23", "--bpm", "123"])
        self.assertEqual(code, 0)
        result = json.loads(output.read_text())
        self.assertEqual(result["source_pattern_revision"], 23)
        self.assertEqual(result["timing"]["coordinate_system"], "source_samples")
        self.assertEqual(result["timing"]["selection_end_frame"], 4)
        self.assertEqual(path.read_bytes(), original)
        self.assertEqual(list(output.parent.glob("*.tmp")), [])


class GitFixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.git("init", "-q")
        (self.directory / ".gitignore").write_text(".local/\n", encoding="utf-8")

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.directory), *args], capture_output=True, check=True)


class SourceAndEvidenceTests(GitFixture):
    def test_unborn_fingerprint_tracks_changes_but_not_local_outputs(self):
        source = self.directory / "source.cpp"
        source.write_text("first\n", encoding="utf-8")
        first = dev.source_state(self.directory)
        self.assertIsNone(first["revision"])
        self.assertTrue(first["dirty"])
        self.git("add", "source.cpp")
        staged = dev.source_state(self.directory)
        self.assertEqual(first["fingerprint_sha256"], staged["fingerprint_sha256"])
        (self.directory / ".local").mkdir()
        (self.directory / ".local" / "output.wav").write_bytes(b"not source")
        self.assertEqual(first["fingerprint_sha256"], dev.source_state(self.directory)["fingerprint_sha256"])
        source.write_text("second\n", encoding="utf-8")
        self.assertNotEqual(first["fingerprint_sha256"], dev.source_state(self.directory)["fingerprint_sha256"])
        source.unlink()
        self.assertNotEqual(first["fingerprint_sha256"], dev.source_state(self.directory)["fingerprint_sha256"])

    def test_cadence_is_separate_from_source_qualification(self):
        now = datetime.now(timezone.utc)
        source = {"fingerprint_sha256": "a"}
        report = {"status": "passed", "source": source, "finished_utc": now.isoformat()}
        self.assertFalse(dev.evaluate_status(report, source, now)["due"])
        changed = dev.evaluate_status(report, {"fingerprint_sha256": "b"}, now)
        self.assertFalse(changed["due"])
        self.assertFalse(changed["source_matches"])
        self.assertIn("focused checks", changed["qualification"])
        self.assertTrue(dev.evaluate_status({**report, "status": "failed"}, source, now)["due"])
        self.assertTrue(dev.evaluate_status(report, source, now + timedelta(hours=24))["due"])
        self.assertTrue(dev.evaluate_status(None, source, now)["due"])
        foreign = {**report, "local_binding_sha256": "different-checkout"}
        self.assertTrue(dev.evaluate_status(foreign, source, now, expected_binding="this-checkout")["due"])

    def test_ci_failure_preserves_real_exit_code_command_and_log(self):
        command = [sys.executable, "-c", "print('intentional fixture failure'); raise SystemExit(7)"]
        with mock.patch.object(dev, "ci_commands", return_value=[("fixture", command)]), redirect_stdout(io.StringIO()):
            self.assertEqual(dev.run_ci(self.directory), 1)
        report = dev.read_latest(self.directory)
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["commands"][0]["argv"], command)
        self.assertEqual(report["commands"][0]["returncode"], 7)
        self.assertIn("intentional fixture failure", (self.directory / report["commands"][0]["log"]).read_text())
        self.assertTrue(dev.ci_status(self.directory)["due"])
        self.assertFalse((self.directory / ".local" / "ci" / "lock.json").exists())

    def test_ci_if_due_does_not_replace_current_evidence(self):
        report = {"status": "passed", "source": dev.source_state(self.directory), "finished_utc": dev.utc_now(), "run_id": "previous", "local_binding_sha256": dev.local_binding(self.directory)}
        dev.atomic_json(self.directory / ".local" / "ci" / "latest.json", report)
        with mock.patch.object(dev, "ci_commands") as commands, redirect_stdout(io.StringIO()):
            self.assertEqual(dev.run_ci(self.directory, only_if_due=True), 0)
        commands.assert_not_called()
        self.assertEqual(dev.read_latest(self.directory)["run_id"], "previous")

    def test_ci_rejects_source_changes_during_otherwise_successful_checks(self):
        command = [sys.executable, "-c", "from pathlib import Path; Path('new-source.txt').write_text('changed')"]
        with mock.patch.object(dev, "ci_commands", return_value=[("fixture", command)]), redirect_stdout(io.StringIO()):
            self.assertEqual(dev.run_ci(self.directory), 1)
        report = dev.read_latest(self.directory)
        self.assertEqual(report["status"], "source_changed")
        self.assertTrue(report["source_changed_during_run"])
        self.assertTrue(dev.ci_status(self.directory)["due"])

    def test_lock_blocks_overlap_and_leaves_unverified_owner_alone(self):
        path = self.directory / ".local" / "ci" / "lock.json"
        with dev.ci_lock(self.directory):
            with self.assertRaisesRegex(RuntimeError, "locked"):
                with dev.ci_lock(self.directory):
                    self.fail("Overlapping CI was allowed")
            self.assertTrue(path.exists())
        self.assertFalse(path.exists())
        dev.atomic_json(path, {"pid": 123456, "host": socket.gethostname(), "token": "old"})
        with mock.patch.object(dev, "pid_is_alive", return_value=False):
            with self.assertRaisesRegex(RuntimeError, "stale lock"):
                with dev.ci_lock(self.directory):
                    self.fail("A stale owner was silently removed")
        self.assertEqual(json.loads(path.read_text())["token"], "old")


class StagedGuardTests(GitFixture):
    def test_guard_reads_staged_secret_even_if_worktree_was_cleaned(self):
        path = self.directory / "config.txt"
        key = "-----BEGIN " + "PRIVATE KEY-----\nfixture\n"
        path.write_text(key, encoding="utf-8")
        self.git("add", "config.txt")
        path.write_text("safe working copy\n", encoding="utf-8")
        count, failures = repo_guard.inspect_staged(self.directory)
        self.assertEqual(count, 1)
        self.assertEqual(len(failures), 1)
        self.assertIn("content", " ".join(failures[0][1]))
        self.assertNotIn("fixture", repr(failures))

    def test_guard_does_not_substitute_unstaged_bad_worktree_content(self):
        path = self.directory / "config.txt"
        path.write_text("safe staged copy\n", encoding="utf-8")
        self.git("add", "config.txt")
        path.write_text("-----BEGIN " + "PRIVATE KEY-----\n", encoding="utf-8")
        self.assertEqual(repo_guard.inspect_staged(self.directory), (1, []))

    def test_guard_rejects_forced_local_audio_and_oversized_blob(self):
        local = self.directory / ".local"
        local.mkdir()
        (local / "take.wav").write_bytes(b"fixture")
        (self.directory / "large.txt").write_text("x" * 32, encoding="utf-8")
        self.git("add", "-f", ".local/take.wav", "large.txt")
        count, failures = repo_guard.inspect_staged(self.directory, max_bytes=16)
        self.assertEqual(count, 2)
        self.assertEqual(len(failures), 2)
        self.assertTrue(any("exceeds" in " ".join(problems) for _, problems in failures))


if __name__ == "__main__":
    unittest.main()
