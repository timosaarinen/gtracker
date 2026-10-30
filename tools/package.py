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
import subprocess
import uuid
import zipfile

if __package__:
    from . import dev
else:
    import dev


def sid_inputs(root: Path) -> list[tuple[Path, str]]:
    backend=root/".local/sid/tools"
    manifest=json.loads((backend/"backend.json").read_text(encoding="utf-8"))
    result=[(backend/"backend.json","sid-tools/backend.json")]
    for name,digest in manifest["files"].items():
        if Path(name).name!=name:raise RuntimeError("Unsafe backend manifest path")
        path=backend/name
        if hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise RuntimeError("SID backend checksum mismatch: "+name)
        result.append((path,"sid-tools/"+name))
    for manifest_name in ("video.json","stream.json"):
        helper=json.loads((backend/manifest_name).read_text())
        result.append((backend/manifest_name,"sid-tools/"+manifest_name))
        for name,digest in helper["files"].items():
            if Path(name).name != name or hashlib.sha256((backend/name).read_bytes()).hexdigest()!=digest:
                raise RuntimeError("Invalid SID helper: "+name)
            result.append((backend/name,"sid-tools/"+name))
    for name in ("goattracker-2.77.tar.xz","libsidplayfp-3.1.1.tar.gz","libresidfp-1.2.3.tar.gz","SDL-source.tar.gz"):
        result.append((root/".local/sid"/name,"sid-tools/source/"+name))
    for name in ("sid_backend.py","sid_render.cpp","sid_stream.cpp","sid_configuration.hpp"):
        result.append((root/"tools"/name,"sid-tools/source/"+name))
    for name in ("examples/sid/heavy-mellow.sidtrk.json","schema/sid-song.schema.json","docs/sid-tracker.md","docs/realtime.md","docs/release.md","CONTRIBUTING.md"):
        result.append((root/name,name))
    return result


def inputs(root: Path, profile: str) -> list[tuple[Path, str]]:
    result = [(dev.executable(root, profile, "gtracker_cli"), "gtracker_cli" + (".exe" if os.name == "nt" else ""))]
    if profile == "desktop":
        result.append((dev.executable(root, profile, "gtracker_editor"), "gtracker_editor" + (".exe" if os.name == "nt" else "")))
        result.append((dev.executable(root, profile, "gtracker_capture"), "gtracker_capture" + (".exe" if os.name == "nt" else "")))
    for name in ("examples/campfire.gtrk.json", "examples/slide-study.gtrk.json", "examples/repluck-study.gtrk.json", "examples/monophonic-study.gtrk.json", "examples/night-bridge.gtrk.json", "examples/night-shift.gsong.json",
                 "examples/night-watch.gsong.json", "examples/night-watch-ember.gtrk.json", "examples/night-watch-lantern.gtrk.json", "examples/night-watch-stars.gtrk.json", "examples/night-watch-road.gtrk.json",
                 "examples/room.json", "scripts/soften.lua", "scripts/slide.lua", "scripts/retain.lua",
                 "schema/pattern.schema.json", "schema/pattern-v2.schema.json", "schema/take.schema.json", "schema/capture-result.schema.json",
                 "schema/alignment-result.schema.json", "schema/production.schema.json", "schema/song.schema.json", "schema/render-cache.schema.json", "schema/note-proposals.schema.json", "schema/proposal-selection.schema.json", "docs/cache.md", "docs/song.md", "docs/production.md", "docs/rendering.md", "docs/runtime.md", "docs/editor.md", "docs/synthesis.md", "docs/gestures.md", "docs/retained.md", "docs/proposals.md", "docs/takes.md", "docs/capture.md", "docs/alignment.md", "THIRD_PARTY_NOTICES.md"):
        result.append((root / name, name))
    result.append((root / "packaging/README.txt", "README.txt"))
    result.append((root / "LICENSE", "LICENSE"))
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
    if profile=="desktop" and os.name=="nt":
        result.extend(sid_inputs(root))
        result.extend([(root/"packaging/start-sid.cmd","Start SID Tracker.cmd"),
                       (root/"packaging/start-guitar.cmd","Start Guitar Editor.cmd")])
    for path, _ in result:
        if not path.is_file():
            raise RuntimeError(f"Package input is missing: {path}. Configure/build the {profile} profile first.")
    return result


def write_package(root: Path, profile: str, output: Path, source: dict, renderer: dict | None = None) -> dict:
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
        "renderer": renderer or {"status": "not_queried", "reason": "Prebuilt-input assembly; the caller did not supply native renderer metadata."},
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


def renderer_info(root: Path, profile: str) -> dict:
    try:
        result = subprocess.run([str(dev.executable(root, profile, "gtracker_cli")), "--version", "--json"],
                                cwd=root, encoding="utf-8", capture_output=True, check=True, timeout=10)
        info = json.loads(result.stdout)
        identity = info["renderer_build_id"]
        if not isinstance(info["renderer_version"], str) or not isinstance(identity, str) or not re.fullmatch(r"[0-9a-f]{64}/.*", identity):
            raise ValueError("Invalid renderer version/build identity")
        return info
    except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError) as error:
        raise RuntimeError(f"Cannot inspect the built renderer metadata: {error}") from error


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
    return write_package(root, profile, output, after, renderer_info(root, profile))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=dev.PROFILES, default="desktop")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    output = args.output or Path(f".local/packages/gtracker-{args.profile}-{platform.system()}-{platform.machine()}.zip")
    try:
        manifest = build_package(dev.ROOT, args.profile, output)
        print(f"Developer package: {output} ({len(manifest['files'])} payload files + manifest; {manifest['source']['revision']})")
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"package: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    dev.utf8_stdio()
    raise SystemExit(main())
