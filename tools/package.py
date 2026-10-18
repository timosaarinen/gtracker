#!/usr/bin/env python3
"""Build and assemble a local relocatable developer ZIP, without publishing it."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import sys
import uuid
import zipfile

if __package__:
    from . import dev
else:
    import dev


def inputs(root: Path, profile: str) -> list[tuple[Path, str]]:
    result = [(dev.executable(root, profile, "gtracker_cli"), "gtracker_cli" + (".exe" if os.name == "nt" else ""))]
    if profile == "desktop":
        result.append((dev.executable(root, profile, "gtracker_editor"), "gtracker_editor" + (".exe" if os.name == "nt" else "")))
        result.append((dev.executable(root, profile, "gtracker_capture"), "gtracker_capture" + (".exe" if os.name == "nt" else "")))
    for name in ("examples/campfire.gtrk.json", "scripts/soften.lua", "schema/pattern.schema.json", "schema/take.schema.json",
                 "schema/capture-result.schema.json", "docs/editor.md", "docs/synthesis.md", "docs/takes.md", "docs/capture.md", "THIRD_PARTY_NOTICES.md"):
        result.append((root / name, name))
    result.append((root / "packaging/README.txt", "README.txt"))
    dependencies = root / ".local/build" / profile / "_deps"
    for source, target in [("gt_json-src/LICENSE.MIT", "json.txt"), ("gt_lua-src/src/lua.h", "lua-header.txt")]:
        result.append((dependencies / source, "licenses/" + target))
    if profile == "desktop":
        for source, target in [("gt_imgui-src/LICENSE.txt", "imgui.txt"), ("gt_implot-src/LICENSE", "implot.txt"),
                               ("gt_sdl-src/LICENSE.txt", "sdl.txt")]:
            result.append((dependencies / source, "licenses/" + target))
        # Preserve SDL's vendored-code notices too; no font/test assets or
        # third-party executables are part of this package.
        sdl_source = dependencies / "gt_sdl-src/src"
        for notice in sorted(sdl_source.rglob("LICENSE*")):
            if notice.is_file():
                result.append((notice, "licenses/sdl-vendored/" + notice.relative_to(sdl_source).as_posix()))
    for path, _ in result:
        if not path.is_file():
            raise RuntimeError(f"Package input is missing: {path}. Configure/build the {profile} profile first.")
    return result


def write_package(root: Path, profile: str, output: Path, source: dict) -> dict:
    """Assemble an already-built profile; callers are responsible for building it."""
    root = root.resolve()
    output = (root / output).resolve()
    if not output.is_relative_to((root / ".local").resolve()):
        raise ValueError("Developer packages must be written under ignored .local/.")
    if output.suffix.lower() != ".zip":
        raise ValueError("Developer package output must end in .zip.")
    payloads = []
    for path, name in inputs(root, profile):
        payloads.append((name, path.read_bytes(), path.stat().st_mode))
    build_directory = root / ".local/build" / profile
    build_options = {}
    cache = build_directory / "CMakeCache.txt"
    if cache.is_file():
        for name in ("CMAKE_BUILD_TYPE", "GT_BUILD_TOOLS", "GT_BUILD_EDITOR", "GT_BUILD_CAPTURE", "GT_ENABLE_LUA", "GT_ENABLE_SANITIZERS"):
            match = re.search(r"^" + name + r":[^=]+=(.*)$", cache.read_text(encoding="utf-8"), re.MULTILINE)
            if match: build_options[name] = match.group(1)
    toolchain = {}
    compiler_files = list((build_directory / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    if compiler_files:
        compiler = max(compiler_files, key=lambda path: path.stat().st_mtime).read_text(encoding="utf-8")
        for variable, name in (("CMAKE_CXX_COMPILER_ID", "id"), ("CMAKE_CXX_COMPILER_VERSION", "version")):
            match = re.search(r'set\(' + variable + r' "([^"]*)"\)', compiler)
            if match: toolchain[name] = match.group(1)
    manifest = {
        "schema": "gtracker.developer-package",
        "version": 1,
        "created_utc": dev.utc_now(),
        "profile": profile,
        "platform": {"system": platform.system(), "release": platform.release(), "machine": platform.machine()},
        "source": source,
        "build_options": build_options,
        "toolchain": toolchain,
        "runtime_libraries": "Platform runtimes are prerequisites; system DLLs/shared libraries are not bundled.",
        "qualification": "Local developer package; see separate verification receipts. No release or cross-platform qualification implied.",
        "files": [{"path": name, "size_bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}
                  for name, payload, _ in payloads],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, payload, mode in payloads:
                entry = zipfile.ZipInfo("gtracker/" + name)
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.create_system = 3
                entry.external_attr = (mode & 0xFFFF) << 16
                archive.writestr(entry, payload)
            archive.writestr("gtracker/package.json", json.dumps(manifest, indent=2) + "\n")
        if dev.source_state(root)["fingerprint_sha256"] != source["fingerprint_sha256"]:
            raise RuntimeError("Source changed while assembling the package; no ZIP was published.")
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)
    return manifest


def build_package(root: Path, profile: str, output: Path) -> dict:
    before = dev.source_state(root)
    targets = ["gtracker_cli"] + (["gtracker_editor", "gtracker_capture"] if profile == "desktop" else [])
    # Explicit targets prevent leftover executables from a now-disabled target
    # from entering an otherwise successful incremental package build.
    code = dev.run_command(root, ["cmake", "--build", "--preset", profile, "--config", "Release", "--target", *targets])["returncode"]
    if code:
        raise RuntimeError(f"Package build failed with exit {code}; no ZIP was published.")
    after = dev.source_state(root)
    if before["fingerprint_sha256"] != after["fingerprint_sha256"]:
        raise RuntimeError("Source changed during the package build; no ZIP was published.")
    return write_package(root, profile, output, after)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=dev.PROFILES, default="desktop")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    output = args.output or Path(f".local/packages/gtracker-{args.profile}-{platform.system()}-{platform.machine()}.zip")
    try:
        manifest = build_package(dev.ROOT, args.profile, output)
        print(f"Developer package: {output} ({len(manifest['files'])} files; {manifest['source']['revision']})")
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"package: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
