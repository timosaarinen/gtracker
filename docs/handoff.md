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

Issue #7 increment: renderer 0.3 has a separate dependency-free `room_v1`
production chain, optional strict v2 settings, shared CLI/editor stereo output,
explicit whole-chain dry bypass, measured clipping/tail diagnostics and a
versioned declarative render identity. The original dry fixture SHA-256 is
unchanged. Settings edit/undo/save/reopen, stale renders and stereo/mono dummy
playback pass. No song cache or arrangement transport is implied yet.

Windows verification: local CI `.local/ci/runs/20261018T045839Z-897eb352/report.json`
passed 14 native and 59 Python tests against its recorded source fingerprint.
The prior attempt caught a stale C-version assertion, which was corrected.
All 21 desktop tests passed separately; see
`.local/nightshift/production-desktop-checks.json`. Actual production controls
were captured and inspected. Linux `b5b1ead` passed all 19 desktop and 12 Clang
ASan/UBSan native tests in the isolated Ubuntu checkout; that precedes production.

Original outputs: `.local/campfire-dry.wav`, `.local/campfire-room.wav` and
`.local/slide-room.wav`. The wet examples are 48 kHz stereo PCM16, unclipped,
with measured quiet endings. On Windows 11 / Intel i7-10700K (8 cores, 16 logical)
/ Clang 18.1.8 Release, three-run median audio-generation times were 15.95 ms
for 352,000 dry frames, 28.31 ms for 446,465 room frames, and 30.35 ms for the
430,465-frame slide/room example. These include production initialization but
exclude instrument setup, metrics, WAV I/O and process startup. No listening
observations or inferred performance improvement are claimed. Full settings,
hashes, metrics and source identity are in
`.local/nightshift/production-measurements.json`; see docs/production.md.

Linux production checkpoint `abc20a0` passed headless CI (14 native, Python59
with13 desktop skips), all21 desktop tests and14 Clang10 ASan/UBSan native tests.
Logs: `.local/qualification/ubuntu/.local/production-{ci,desktop,sanitize}.log`.

The next issue #6 foundation extracts the shared `gtracker_renderer` with copied
absolute-frame schedules, persistent core/room state, bounded block requests,
reset and EOF. Pattern CLI/editor now use it too; the original dry and wet WAV
bytes remain identical. `examples/runtime_render.cpp` builds with the core-only
profile and no authoring/desktop dependencies. Six core-only/example tests and
focused pattern/production integration tests pass. See docs/runtime.md. Song
arrangement, streamed WAV writing and persistent caching are the next increments.

Streamed pattern WAV export is now usable through `render --stream`, with exact
frame previews, bounded blocks, JSON stderr progress, cancel-file/SIGINT handling
and atomic publication. Native and wrapper cancellation retain old output;
handled failures remove their temporary WAVs. Shared file writing also retains
UTF-8 and existing save/capture semantics. The source renderer's original dry
and wet PCM is unchanged. See docs/rendering.md.

Windows full CI at `.local/ci/runs/20261018T051632Z-e331f280/report.json` passed16
native and64 Python checks (one POSIX-only interrupt skip). After the final
interrupt initialization and injected-write test adjustments, focused native
export, refreshed capture/UI executables, eight desktop checks and all streaming
Python cases passed; `.local/nightshift/stream-checks.json` binds those bytes.
Windows console Ctrl+C remains unqualified; POSIX interrupt verification is next.

Linux `fe8ed5b` passed headless CI (16 native, all64 Python with no skips),
23 desktop tests and16 Clang10 ASan/UBSan native tests. Direct and developer-
wrapper targeted POSIX SIGINT cancellation both passed. Logs are the isolated
Ubuntu checkout's `.local/stream-{ci,desktop,sanitize}.log`; Windows console
Ctrl+C and all physical audio checks remain separate.

Issue #6 song increment: renderer0.4 adds strict version1 song arrangements,
relative pattern references or embedded definitions, explicit repeat/timing/
fret/strength variations, one global instrument/tempo/seed/production, portable
snapshots, full source-write guards and the same bounded native WAV exporter.
There is no reset between placements. Pattern IDs/points/source files remain
unchanged; incompatible instruments and per-pattern production fail explicitly.
CLI `validate-song`, `render-song` and `snapshot-song` have developer wrappers.

The original `examples/night-shift.gsong.json` renders to `.local/night-shift.wav`:
68 beats/46.468 seconds including tails, 48 kHz stereo PCM16, 2,230,465 frames,
peak0.15143, no full-scale samples and ending50ms peak5.1e-12. Its complete source
snapshot is8,940 UTF-8 bytes versus8,921,860 PCM bytes. This is authored synthetic
music, with no transcription/listening claim. Native tests compare one-pattern
and flattened references, ringing into an empty pattern, room continuity,
variations, seed/tempo ownership and snapshot identity. Eight generated Python
song cases cover moved references, standalone snapshots, warmed previews,
source-write refusal, strict future-data rejection and cancellation.

Windows headless CI `.local/ci/runs/20261018T053403Z-5013690d/report.json` passed17
native and72 Python tests (one POSIX-only skip), with freshly built desktop
executables present. Persistent cache storage/recovery, arrangement UI and
seamless loop preparation remain next work; see docs/song.md.

The optional local PCM cache now handles patterns and songs through the same
streamed export. It checks canonical source/options/crop/build identity, receipt
checksum, PCM header/size and full streamed SHA-256, then hashes the copied bytes
again before publishing output. Read-only misses/corruption render independently;
write failures preserve a successful output with `store_failed`. Handled
interruption leaves old output intact, including during cache hashing/copying.
Cache hits report zero new generation/warm-up and label reused float metrics.
No eviction or acoustic-state checkpointing is implied. See docs/cache.md.

CMake native source/toolchain/configuration fingerprints now include dirty code
and automatically regenerate on source changes. `--version --json`, render
reports and developer packages expose the compiled identity. Incremental SHA-256
retains the original source hashes. A Windows Unicode filesystem error exposed
and fixed a normal-result JSON reporting failure during corrupt-cache recovery.
Native cache checks also inject a PCM change between validation/copying, corrupt
or deep receipts, write/callback failures, and cancellation before/after storage.

Windows full CI `.local/ci/runs/20261018T060055Z-fb04ee1c/report.json` passed18
native and80 Python tests with one POSIX-only interrupt skip. Cache-source
corruption/recovery and the first miss/reused hit passed separately. Fresh
desktop/core-only, package and Linux checks continue below in local receipts;
do not infer physical playback, microphone, Mac or listening qualification.

The clean cache checkpoint `1f19e17` also passed Ubuntu GCC9 headless/Python80,
desktop25 and Clang10 ASan/UBSan18, with no Python skips after rebuilding the
desktop tools. A private-clone audit changed production code without bumping the
renderer version: an ordinary build regenerated its identity and missed cache;
restoring the source restored the original identity/hit/PCM. Debug versus Release
also selected different keys. The clone was restored byte-for-byte. Evidence:
`.local/qualification/ubuntu/.local/nightshift/build-identity-b46e89af/report.json`.

Named Windows streaming measurements retained about 5.5 MB peak child working
set for sparse 26/98/770-second exports. The 770-second baseline took 32.34 seconds
including process/export overhead; one-second previews stayed around 16–20 ms.
Three full 46.47-second stereo song exports took 379.74–382.99 ms with cache
refresh versus 112.15–114.01 ms for checked hits. These are actual workloads on
the i7-10700K/Clang18 Release machine, not an inferred general speedup. Source,
commands and measurements are in `.local/nightshift/stream-measurements/report.json`.

Issue #4 increment: renderer0.5 adds explicit `retained_0_5` excitation memory,
the existing slide API, an editor/Lua selector, an original dry repluck study and
optional generated known-score probes. Original baseline/contact factories,
numeric paths and campfire/slide PCM16 hashes remain intact. Numeric cleanup in
the new model makes an isolated 98-second body tail much cheaper with identical
PCM16; full parameters and limits are in docs/retained.md. This is an experimental
excitation heuristic, not physical pick/fret geometry or a listening verdict.

Windows full CI `.local/ci/runs/20261018T063743Z-67bbff05/report.json` passed19
native and84 Python checks (one POSIX-only interrupt skip) against the recorded
dirty source fingerprint. Fresh desktop26, core-only7 and44-file package builds
passed separately. The final extra framebuffer capture is checked separately.
`.local/nightshift/retained-checks.json` binds the generated A/B, 18 known-score
probes and playable `.local/night-shift-retained.wav` to their actual source/build.
Linux qualification of this new model follows its committed checkpoint.
