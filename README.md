# gtracker

An acoustic-guitar tracker and offline synthesizer, starting as a music-authoring
tool for **Lust of War**. Compose in horizontal tabs, use Lua for musical editing,
and develop audio-to-pattern analysis against the same native instrument that
will render music in the game.

**Status: working 0.5 prototype.** This is an audible development foundation,
not yet a realistic guitar instrument or an automatic guitar transcriber.

## What works now

- C++17 plucked-string renderer with a small C ABI, deterministic excitation,
  fractional frets, damping and synthetic body taps; no stored audio samples.
- Versioned JSON patterns, strict validation, stable event IDs, beat timing,
  explicit tuning and seed, and mono/stereo 16-bit PCM WAV export.
- Explicit v2 [continuous slide curves](docs/gestures.md) through save/reopen,
  Lua, native rendering and tab editing/undo. The experimental contact model
  retains vibration; the original v1 baseline remains selectable and unchanged.
- An explicit [retained-excitation model](docs/retained.md) preserves part of a
  string's vibration when replucked, with a dry A/B study and repeatable generated
  pitch/decay probes. It remains experimental and separately selectable.
- Native SDL3 + Dear ImGui/ImPlot editor: horizontal string/body lanes, event
  selection and editing, add/delete, undo/redo, load/save, cancellable asynchronous rendering,
  queued playback with session output selection, waveform preview, keyboard shortcuts, grouped control undo,
  an unsaved-changes dialog for loading/closing, and periodic recovery score copies.
- Embedded Lua 5.5.1 commands for tempo, transposition, attack strength and note
  creation, shared by the CLI and editor. A failed command leaves the score intact.
- Python analysis worker that accepts a recorded WAV and returns a versioned
  result with source hash, timing, channel/peak/RMS metadata and job provenance.
  It explicitly reports that transcription is unavailable.
- A separate opt-in [single-note proposal experiment](docs/proposals.md), with
  explicit channel selection, original sample timing, string/fret alternatives
  and reviewed batch acceptance/undo. Generated two-bar fixtures pass; chords,
  articulation recognition and real-guitar accuracy remain unqualified.
- Source-preserving take import/inspection with relative bundle paths, explicit
  source-sample timing, manual/uncalibrated offsets and checked analysis identity.
  A native PCM/take reader verifies hashes and formats without Python at playback;
  the editor overlays source/synth waveforms and compares original mono/stereo PCM.
- A separate native SDL3 [capture tool](docs/capture.md) with explicit input
  selection, nominal sample timing, count-in/metronome and partial recovery;
  `dev.py record-take` connects it to checked bundles and metadata-only analysis.
  Generated/dummy input checks are distinct from real-device qualification.
- An original broadband loopback reference and [bounded file-delay estimator](docs/alignment.md)
  with match/ambiguity diagnostics and source hashes. Physical latency and clock
  drift remain separately measured work; authoring offsets stay explicit.
- Native [stereo room production](docs/production.md) with saved settings,
  wet/dry balance, decay/damping, pre-delay, width, gain and an exact dry bypass.
  CLI/editor rendering shares the same chain and reports signal/export metrics.
- [Bounded streamed WAV export](docs/rendering.md), exact previews with native
  state warm-up, progress and handled cancellation that preserves old output.
- [Song arrangements](docs/song.md) with reusable patterns, explicit variations,
  portable snapshots and continuous instrument/room state. Original examples
  include a 46-second sketch and **Night Watch**, a two-minute fingerstyle study
  using four editable phrases through the same native runtime path.
- An optional [persistent PCM cache](docs/cache.md), checked against source,
  renderer build, parameters, crop bounds and complete WAV hashes, with recovery
  and read-only access modes.
- Agent instructions, a portable development CLI, local CI receipts, a staged
  repository guard, issue templates and manual cross-platform qualification.

The core supports 1–12 independent string slots. Twelve strings are **not yet**
six paired courses. Bends, hammer-ons, pull-offs, physical contact/collision models,
microphone recording in the editor and an arrangement UI are roadmap
work. The existing model is a reference to compare with future physical models.

## Build and listen

Requirements: a C++17 compiler and C compiler, CMake 3.25+, Ninja, Python 3.10+,
and network access for the first dependency fetch. Native dependency archives
are version-pinned and SHA-256 checked in CMake. No ML installation is required.

On Windows use a Visual Studio developer PowerShell/terminal with the C++ build
tools and Windows SDK available. On macOS install Xcode command-line tools and
CMake/Ninja. Linux desktop builds also need SDL's windowing/audio development
libraries; the package list is in the manual qualification workflow. Python may
be named `python3` on your system; the commands below use `python`.

From the repository root, the quickest audible start is:

```sh
python tools/dev.py doctor
python tools/dev.py configure
python tools/dev.py build
python tools/dev.py test
python tools/dev.py render --output .local/demo.wav --json
```

Play `.local/demo.wav` in an audio player. This default **headless** profile builds
the instrument, document/CLI tools and Lua without desktop dependencies.

For the native tracker:

```sh
python tools/dev.py configure --profile desktop
python tools/dev.py build --profile desktop
python tools/dev.py editor examples/campfire.gtrk.json
```

Choose **Render + Play**, click a fret number, edit it in the inspector, and render
again. Save your own work under `.local/`. See [editor controls](docs/editor.md).

After configuring the desktop profile, make a local developer ZIP with:

```sh
python tools/dev.py package --profile desktop
```

The ZIP is under `.local/packages/`; it includes the tools, original example,
scripts and dependency notices. Extract the whole folder and move it as needed.
The packaged editor resolves resources beside its executable and suggests a
per-user save folder, so the installation can be read-only. System runtime
libraries remain prerequisites. This command does not publish a release.

The initial phrase is original test material. The renderer produces the audio;
there are no committed sample recordings or sound banks.

## Use the tools directly

Executables are in `.local/build/<profile>/bin/` (with `.exe` on Windows):

```sh
.local/build/headless/bin/gtracker_cli validate examples/campfire.gtrk.json --json
.local/build/headless/bin/gtracker_cli render examples/campfire.gtrk.json --output .local/demo.wav --sample-rate 48000 --tail 2 --json
.local/build/headless/bin/gtracker_cli transform examples/campfire.gtrk.json --script scripts/soften.lua --output .local/soft.gtrk.json --json
```

The CLI's JSON reports are suitable for agents and batch experiments. Lua scripts
are trusted local code with ordinary Lua standard libraries; loading a pattern
does not execute scripts. The explicit `--script` operation selects one.

Try the analysis interface using the synthetic demo before a personal recording:

```sh
python analysis/analyze.py --input .local/demo.wav --output .local/analysis.json --job-id first-take --pattern-revision 1 --bpm 90
```

This currently measures the WAV and returns `status: metadata_only`, with no note
events. Read [the analysis protocol](analysis/README.md) before adding a recognizer.
Original audio remains separate from the compact score that the game will use.
Use [preserved-take import](docs/takes.md) to keep a WAV, its timing, metadata-only
analysis and an editable pattern copy together under ignored `.local/takes/`.

## Working with Codex or other agents

Start with [AGENTS.md](AGENTS.md) and the [current handoff](docs/handoff.md).

```sh
python tools/dev.py ci-status
python tools/dev.py ci --if-due
```

Local checks are the ordinary authority. A recent daily pass and verification of
new edits are distinct: run appropriate focused checks after changes. Logs and
source-bound receipts remain in ignored `.local/ci/`.

The GitHub Actions workflow is **manual only** and supplements local checks on
Windows, Linux and Intel macOS. A configured matrix is not evidence those hosts
have passed. There is no scheduler, remote listener or automatic agent launcher.
Finished verified changes normally go directly to `main`, preserving published
history. See [the development workflow](docs/workflow.md).

## Project map

| Location | Purpose |
| --- | --- |
| `include/gtracker/core.h`, `src/core.cpp` | Independent C ABI and C++ instrument |
| `include/gtracker/project.hpp`, `src/project.cpp` | Editable score, validation, native event compilation, WAV export |
| `src/cli.cpp` | Unattended validate/render/transform commands |
| `src/editor.cpp`, `src/lua_commands.cpp` | Native editor and coarse Lua commands |
| `examples/`, `scripts/`, `schema/` | Original score, Lua example, format schema |
| `analysis/` | Separate analysis process and documented result contract |
| `tests/`, `tools/` | Signal/format/CLI/workflow checks and agent entry point |
| `docs/` | Architecture, current evidence, controls and roadmap |

The dependency-free instrument can be built alone with the `core-only` CMake
preset. `sanitize` enables ASan/UBSan on supported GCC/Clang Unix hosts.

## Design and next work

- [Architecture and boundaries](docs/architecture.md)
- [Pattern format](docs/format.md)
- [Current synthesis model and limits](docs/synthesis.md)
- [Roadmap](docs/roadmap.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

The project's own distribution license has not been selected. Dependencies retain
their respective licenses; do not infer a license grant for gtracker from them.
