# gtracker

**Write guitar and SID music in a native desktop tracker.**

gtracker combines a three-voice Commodore 64 SID tracker with an experimental
acoustic-guitar pattern editor. Compose with the keyboard, arrange reusable
phrases, hear notes as you enter them, and export audio or shareable videos.
It began as a music-authoring tool for *Lust of War*.

**Status: Windows release candidate.** Realtime synthesis and exports have native
and automated coverage. Physical device/listening qualification and the project
license decision remain release gates; see [release readiness](docs/release.md).

## Start from the repository

Install Python 3.10+, CMake 3.25+, Ninja, Bun (optional), and a C++17 toolchain.
On Windows, use a Visual Studio developer terminal with the Windows SDK.
The SID backend also needs MSYS2 at `C:/msys64`, with i686 GCC/SDL and UCRT64
GCC/autotools. First setup downloads checksum-pinned dependencies.

```sh
bun run dev
```

This configures and incrementally builds the editor, prepares missing Windows
SID helpers, and opens a new SID song. No `bun install` is needed.
Without Bun, run `python tools/dev.py dev`.

```sh
bun run dev examples/sid/heavy-mellow.sidtrk.json
bun run dev --guitar
bun run dev --ui-scale 2
```

The original **Heavy and Mellow** song is an editable two-minute starting point.
Use **Save As** for your own version. Development data and generated exports
belong under ignored `.local/`. The editor defaults to 200% on 4K displays;
the toolbar scale button remembers your preferred size.

## Make your first SID pattern

1. Choose a preset, select a grid cell, and leave **Edit** enabled.
2. Play the lower keyboard row `Z S X D C V G B H N J M`, or the upper row
   `Q 2 W 3 E R 5 T 6 Y 7 U`. Notes audition through the real SID emulator.
3. Use `A` for note-off, Delete to clear, and Tab to change voice.
4. Press **F6** to loop the pattern, **F5** to play the song, or **F8** to stop.
   Space toggles playback/pause. Arrange patterns in the order panel.
5. Save with Ctrl+S. Use **Export SID** or **More** for WAV, runnable C64 PRG,
   MP4 video, or diagnostic GoatTracker SNG.

MP4 export adds a permanent title, three voice oscilloscopes and a full-screen
FFT spectrum. It produces 1080p H.264/AAC with fast-start metadata. Install
FFmpeg on PATH, or set `GTRACKER_FFMPEG` to its executable. Exports stay local;
you choose where to upload them.

Read the [SID guide](docs/sid-tracker.md) for selection, undo, sections, recovery,
export requirements and format limits.

## Guitar authoring

Use `bun run dev --guitar` for horizontal string/body lanes. Enter frets, edit
articulations and production, and press Space to play continuous native
synthesis. **Audition notes** previews entered or selected notes. Render remains
available for waveform inspection; it is not required before playback.

The guitar instrument is a physical-synthesis research baseline, not a finished
realistic acoustic instrument. Continuous slides, retained excitation and room
production are explicit selectable models. Automatic guitar transcription,
paired twelve-string courses and an arrangement editor are not implemented.

- [Editor controls](docs/editor.md) and [pattern format](docs/format.md)
- [Song arrangements and original examples](docs/song.md)
- [Synthesis models](docs/synthesis.md), [slides](docs/gestures.md),
  [retained excitation](docs/retained.md), [room production](docs/production.md)
- [Recording](docs/capture.md), [preserved takes](docs/takes.md),
  [experimental note proposals](docs/proposals.md)

## Architecture and development

The dependency-free C++ instrument core feeds both realtime guitar playback
and offline export. SID playback uses a persistent external libsidplayfp/reSIDfp
process and bounded PCM blocks; SID export uses the same pinned emulator and
GoatTracker player. Audio synthesis stays off the UI thread.
See [realtime playback](docs/realtime.md) for latency evidence and limits.

```sh
python tools/dev.py doctor
python tools/dev.py configure --profile desktop
python tools/dev.py build --profile desktop
python tools/dev.py test --profile desktop
python tools/dev.py ci --if-due
python tools/dev.py package --profile desktop
```

The `headless` preset builds CLI/core tools without the editor. `core-only`
builds the independent C ABI. Local checks are authoritative; GitHub Actions
are manually triggered cross-platform supplements.

| Location | Responsibility |
| --- | --- |
| `src/core.cpp`, `include/gtracker/core.h` | Independent instrument and C ABI |
| `src/renderer.cpp`, `src/project.cpp` | Continuous rendering and guitar documents |
| `src/sid/` | SID documents, compilation, backend, video and editor |
| `src/editor.cpp` | Guitar editor and application host |
| `tools/` | Development, backend preparation, verification and packaging |
| `examples/`, `schema/` | Original editable scores and versioned formats |
| `analysis/` | Optional Python authoring experiments |

Read [CONTRIBUTING.md](CONTRIBUTING.md), [architecture](docs/architecture.md),
[workflow](docs/workflow.md), and [AGENTS.md](AGENTS.md) before making changes.
Please report bugs with a small original score, reproduction steps, operating
system and source revision. Keep personal recordings and credentials out of Git.

## Distribution and licenses

The project license is awaiting the owner's decision. Public repository
visibility alone does not grant an open-source license. Dependencies and external
helpers retain their own licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).
No downloaded binaries, ROMs, model weights or recordings are committed.
