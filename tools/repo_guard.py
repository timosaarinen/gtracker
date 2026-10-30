#!/usr/bin/env python3
"""Check staged Git blobs, not their potentially different working-tree copies."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
MAX_BYTES = 2 * 1024 * 1024
# Explicitly requested README artwork. Only this reviewed image may bypass the
# text-only/size rules; changing the file requires reviewing its digest again.
APPROVED_ARTWORK = {
    "docs/assets/gtracker-banner.png": "294afa607001bfd687edf93d50e58474332a97ed2e6be212913dba7cc5b8913b",
}
FORBIDDEN_SUFFIXES = {
    ".mp4", ".prg", ".sid", ".wav", ".flac", ".mp3", ".aif", ".aiff", ".ogg", ".opus", ".m4a",
    ".onnx", ".pt", ".pth", ".ckpt", ".safetensors", ".h5",
    ".exe", ".dll", ".so", ".dylib", ".o", ".obj", ".a", ".lib", ".pdb", ".pyc", ".pyo",
    ".zip", ".7z", ".tar", ".gz", ".bz2", ".xz",
}
SECRET_PATTERNS = [
    re.compile(rb"-----BEGIN (?:[A-Z ]+ )?PRIVATE KEY-----"),
    re.compile(rb"\bgh[pousr]_[A-Za-z0-9]{36,255}\b"),
    re.compile(rb"\bgithub_pat_[A-Za-z0-9_]{40,255}\b"),
    re.compile(rb"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b"),
]


def git(root: Path, *args: str) -> bytes:
    result = subprocess.run(["git", "--literal-pathspecs", "-C", str(root), *args], capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace").strip() or "Git command failed")
    return result.stdout


def path_problems(name: str) -> list[str]:
    path = PurePosixPath(name.lower())
    problems = []
    if any(part in {".local", "build", ".venv", "node_modules", "__pycache__", "recordings", "models"} or part.startswith(("build-", "cmake-build-")) for part in path.parts):
        problems.append("generated/private directory; use ignored .local/")
    if path.suffix in FORBIDDEN_SUFFIXES:
        problems.append("audio, model, archive, or compiled artifact; generate/store under .local/")
    basename = path.name
    if (
        (basename == ".env" or basename.startswith(".env.")) and basename not in {".env.example", ".env.sample"}
        or basename in {"id_rsa", "id_ed25519", "credentials.json", "credentials", "secrets.json"}
        or path.suffix in {".pem", ".key", ".p12", ".pfx"}
    ):
        problems.append("credential/private-key filename")
    return problems


def inspect_staged(root: Path, max_bytes: int = MAX_BYTES) -> tuple[int, list[tuple[str, list[str]]]]:
    names = git(root, "diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z").split(b"\0")
    failures = []
    count = 0
    for raw in names:
        if not raw:
            continue
        name = raw.decode("utf-8", errors="surrogateescape")
        count += 1
        problems = path_problems(name)
        index_entry = git(root, "ls-files", "--stage", "-z", "--", name)
        if index_entry.startswith(b"160000 "):
            problems.append("submodule entry requires a deliberate repository policy change")
        else:
            size = int(git(root, "cat-file", "-s", ":" + name).strip())
            artwork = APPROVED_ARTWORK.get(name)
            if artwork and size <= 4 * 1024 * 1024:
                data = git(root, "show", ":" + name)
                if hashlib.sha256(data).hexdigest() != artwork:
                    problems.append("artwork differs from the explicitly reviewed image")
            elif size > max_bytes:
                problems.append(f"staged blob exceeds {max_bytes} bytes ({size} bytes)")
            else:
                data = git(root, "show", ":" + name)
                if b"\0" in data:
                    problems.append("binary content; commit original text fixtures and generate binary fixtures")
                if any(pattern.search(data) for pattern in SECRET_PATTERNS):
                    problems.append("possible credential/private-key content (value withheld)")
        if problems:
            failures.append((name, problems))
    return count, failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--max-bytes", type=int, default=MAX_BYTES)
    args = parser.parse_args(argv)
    if args.max_bytes < 1:
        parser.error("--max-bytes must be positive")
    try:
        count, failures = inspect_staged(ROOT, args.max_bytes)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"repo guard error: {error}", file=sys.stderr)
        return 1
    for name, problems in failures:
        print(f"REJECT {name!r}")
        for problem in problems:
            print("  - " + problem)
    if failures:
        print(f"Repo guard rejected {len(failures)} of {count} staged paths.")
        return 1
    print(f"Repo guard passed: {count} staged paths inspected; no working-tree substitutions.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
