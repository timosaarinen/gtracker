# Current handoff

## Initial bootstrap — 2026-10-18

The starting architecture is implemented: independent C++ instrument/C ABI,
JSON document layer, offline CLI, SDL3/ImGui/ImPlot tab editor, embedded Lua editing,
and a Python analysis-result interface. Read README.md for the exact commands and
current scope; AGENTS.md carries the working rules adapted from MMO.

The example is an original eight-beat phrase. At 90 BPM and a two-second render
tail it produces 352,000 mono samples at 48 kHz (about 7.33 seconds). No recorded
sound, weights, binary dependencies or caches are committed.

## Verification boundary

Bootstrap verification is on Linux x86-64 with GCC 13.3, CMake 3.31.6 and Ninja.
The source archives are pinned and checked by SHA-256. The headless targets cover
core behavior, the real C header/link boundary, pattern validation and persistence,
sample timing, WAV export, and successful/failed Lua transactions. Python checks
cover PCM analysis, actual failure receipts, CI cadence/qualification and staged
repository-guard semantics. See the current local receipt for exact run evidence.

The native desktop target has compiled here. Its window backend was explicitly
configured without X11/Wayland development libraries and exercised using SDL's
dummy video driver and software renderer. An actual 1152×760 UI image was captured
and inspected, including the string/body lanes, selected event inspector and
rendered waveform. The bounded smoke rendered the score and completed its frames.
This does not prove interactive input, physical playback, microphone capture,
hardware graphics, Windows behavior or Intel Mac behavior.

Core ASan/UBSan checks passed with leak detection disabled. LeakSanitizer's process
inspection was blocked by the bootstrap environment; no leak-check result is
claimed. Other platforms and actual musical listening remain qualification work.
Hosted workflow configuration alone is not a cross-platform pass.

## Next useful work

Start with [platform qualification (#1)](https://github.com/timosaarinen/gtracker/issues/1)
on the Windows development machine and Intel Mac, then add
[short-take recording (#2)](https://github.com/timosaarinen/gtracker/issues/2)
with source-sample timing and an analysis proposal workflow.
Use [the roadmap](roadmap.md) and GitHub issues for bounded tasks and acceptance
criteria. The near-term musical target is one recorded phrase becoming editable
tabs and then resynthesizing through this same C++ instrument.

The analyzer intentionally returns metadata only, not a transcript. The current
physical model has independent strings, static frets, damping and body taps.
Continuous gestures, realistic contact/body coupling, paired courses, arrangement
and room production are separate follow-up milestones.

## Suggested first Codex session

```text
Work in timosaarinen/gtracker. Read AGENTS.md, README.md, docs/handoff.md and the
selected issue. Run doctor and ci-status, then ci --if-due when required. Inspect
the current source before changing it. Make one small usable improvement, expose
new experimental behavior through files/CLI options, verify the actual affected
behavior, and commit/push finished work to main. Keep personal recordings, model
weights and generated output under .local/. Report commands actually run, the
commit, remaining limits, and the next useful step.
```

Keep this handoff short and current. Put model details and durable designs in the
specialist docs; put transient machine paths/process IDs in ignored local notes.
