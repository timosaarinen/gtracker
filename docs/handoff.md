# Current handoff

## Night shift — 2026-10-18 (in progress)

The requested work window ends at 09:51 UTC. Read README.md and AGENTS.md for
architecture and working rules; issues #1–#7 contain the planned milestones.

Shipped on main: `ff2e36f` adds guarded load/close, grouped control undo,
content-based savepoints and native/synthetic UI regressions. `e3390ef` fixes
Windows UTF-8 CLI/Lua paths and JSON failures that previously terminated on
missing Unicode files or arbitrary-byte Lua errors. BOM/shebang script line
numbers are retained. No instrument/DSP change has been made in these commits.

The developer-package increment adds `python tools/dev.py package`, per-user
save paths and executable-relative resources. The development runner explicitly
uses ignored `.local/` for user work and the repository for resources. ZIPs carry
source fingerprints, build options/toolchain, per-file hashes and dependency
notices. Failed builds/source changes preserve the prior ZIP. Native system
runtime libraries remain prerequisites; this is not a public release.

## Actual local evidence

Windows 11 AMD64, Clang 18.1.8, CMake 3.30.0-rc4, Ninja 1.11.1, Python 3.12.6:

- Headless/desktop Release builds passed. After the history refactor,
  `ctest --preset desktop --build-config Release --output-on-failure` passed all
  nine tests. Focused project/CLI/Lua tests passed after the UTF-8 changes.
- The current editor-path changes passed focused `editor_ui` and `editor_smoke`.
  The native UI test injects synthetic mouse/keyboard input, tests guarded close,
  grouped undo, stale-render rejection, dummy audio queuing, and writable-data
  save/reopen. Actual modal/render framebuffer captures were inspected.
- `python -m unittest discover -s tests -p 'test_*.py' -v` passed 22 tests,
  including native Unicode CLI execution and developer-package preservation.
- A moved developer ZIP passed from an unrelated working directory with Windows
  ACL write/create denial enforced on its private extracted copy. CLI
  transform/save/reopen/render and editor dummy smoke succeeded; bundled bytes
  stayed unchanged. Temporary ACLs were restored. Evidence, exact commands and
  source fingerprints are in ignored `.local/nightshift/`, including
  `relocation-96d5365c/report.json`. That report records a dirty source fingerprint,
  not a claim that the pre-edit commit alone contained the package changes.

The original `.local/demo.wav` remains playable: renderer 0.1.0, seed 42,
90 BPM, 48 kHz/16-bit mono, eight beats plus a two-second tail (352,000 frames).
Generated audio, builds, ZIPs and local reports remain ignored.

## Limits and next work

Issue #2 now has a CLI take-import/inspection increment: exact source WAV and
pattern copies, versioned relative-path manifests, explicit source-frame mapping,
manual/uncalibrated input offsets and metadata-only job identity. Eight focused
tests pass for 44.1/48 kHz mono/stereo PCM widths, timing, moved bundles, stale
pattern revisions, changed source/jobs, refused overwrites and cancellation.
See docs/takes.md. Native source comparison now opens with `editor --take PATH`
or the Source take inspector. It overlays source/synth on a beat axis, retains
pre-roll, switches original stereo/rate to the mono synth and back, and preserves
the take's pattern association on save/reopen. The generated stereo UI fixture
passes guarded-load cancellation, A/B transport, save/reopen and source-byte
preservation checks; actual comparison frames were inspected. The separate SDL3
capture CLI now implements explicit input selection, PCM16 application capture,
count-in/metronome, retained context and cancelled/disconnected partials. File
simulation opens no device; dummy drivers are labelled. Generated 44.1/48 kHz
mono/stereo PCM and recovery checks pass. Real-device capture remains untested;
input/output clock alignment is uncalibrated. See docs/capture.md. Musical
transcription remains unavailable.
`dev.py record-take` now connects completed capture to the preserved bundle and
analysis job. It verifies snapshot/source/reference identities, retains raw
recovery on cancellation or analysis failure, and publishes only a complete
checked take. Generated integration checks cover pattern edits during capture,
partial recovery, tampered receipts and native take reopen. The editor can open
the resulting normal v1 take manifest; recording controls are still CLI-only.
The native CLI now also exposes `take-info` using the shared bounded PCM reader,
SHA-256 source verification and checked job/timing/metrics. Focused PCM tests
cover channel/width/rate variants, unknown chunks, truncation and buffer bounds;
the take integration cases compare native hashes, metrics and beat mapping with
the Python importer/inspector. Preview conversion leaves source files untouched.

No physical window interaction, hardware playback/listening or microphone capture
is claimed. The default UI font lacks some non-Latin glyphs even though Unicode
filenames round-trip. The analyzer is metadata-only; no transcription exists.
The baseline still has independent strings, static frets, damping and body taps.

The manual hosted workflow now includes dummy desktop tests and developer ZIPs,
but the current GitHub CLI token received HTTP 403 listing Actions. No hosted
night-shift result is claimed. Bootstrap Linux GCC 13.3 headless/core/dummy-UI and
ASan/UBSan evidence covers the earlier bootstrap source, not these new edits;
LeakSanitizer was blocked there. Intel Mac and current cross-platform
qualification remain outstanding. Keep issue #1 open for those real checks.

New Linux evidence now exists for committed `8f45804`: Ubuntu 20.04.6 under
WSL2 6.18.54.1, GCC 9.4, Python 3.11.9, CMake 4.3.3, Ninja 1.10. Headless CI
passed 11 native tests and Python's 53 with 13 desktop skips; after a full SDL
desktop build, all 18 desktop tests and all 53 Python tests passed without skips.
The original 48 kHz campfire WAV has the same SHA-256 as the Windows fixture.
Logs are in `.local/qualification/ubuntu/.local/`; this does not qualify later
source bytes or physical Linux windows/audio devices. The GCC 9 sanitizer run
hit a pre-main ASan/dynamic-loader fault, confirmed in a bounded debugger run;
no passing sanitizer result is claimed for that runtime. Alternate-runtime
checks are separate: Clang 10 ASan/UBSan passed all 11 native tools/core tests
with default leak settings, as well as the three core-only tests. Intel Mac
qualification is still outstanding.

Next: continue the planned production reverb/arrangement work in small verified
increments, retaining explicit dry references and shared native rendering.
Real takes, clock/latency calibration and transcription need separate evidence.

The native CLI now generates original deterministic broadband probes and checks
bounded relative file delay with source hashes, selected channels, polarity and
ambiguous/boundary/no-match states. Capture's optional `--calibration-probe`
queues and saves that reference on the nominal input timeline. Generated routed
comparison tests recover the injected sample delay while leaving the take
uncalibrated. Equal rates, matched files and the one-frame search resolution do
not qualify physical input/output latency or drift. See docs/alignment.md.

Issue #3 core increment: `gt_create_model` explicitly selects `contact_0_2`;
the legacy factory preserves `baseline_0_1`. Copied per-string slide points move
contact without re-exciting vibration; damping, reset and render partitioning
are covered by native tests. Core-only builds remain dependency-free. Pattern v2
now carries explicit model selection and local continuous slide points. The CLI,
Lua and editor use the same renderer. The tab shows/selects slides; detailed
unsnapped point edits, grouped undo, linked initial frets and save/reopen are
implemented. V1 loading/saving stays v1. Unknown future gestures/models fail;
sub-frame point collisions are rejected rather than flattened. The original
slide study renders to `.local/slide-study.wav`. See docs/gestures.md. This is
still an experimental moving-delay model, without a listening/realism claim.

The latest editor polish shares a single beat range and actual plot data
rectangle between tab/source/synth. Pattern/context fit, pointer zoom, bounded
middle-drag pan, compact resizing and display-only wave auto scale are available.
Native math and synthetic UI checks cover pixel alignment through navigation,
unchanged document generation/history/source bytes and refused insertion in
retained context. The inspector now uses the full side-column height.
