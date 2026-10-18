# Architecture

## Goal

Record short acoustic-guitar phrases into tracker patterns, correct and shape
their playing gestures in horizontal tablature, and render convincing music from
compact performance data. Full songs will arrange the same patterns and preserve
ringing/contact/room state across musical boundaries.

The first end-to-end target is: play two bars, inspect a proposed pattern, correct
a fret or articulation, render through the canonical instrument, and A/B it
against the original take. Current code covers manual score authoring and native
resynthesis; capture and transcription remain explicit follow-up work.

## Boundaries that already exist

**gtracker_core** contains the C++ instrument and exposes a small C ABI. It owns
its physical state, sample clock, event copy and seeded excitation. It renders
blocks without allocating. It has no dependencies on the editor, device layer,
Lua, Python, JSON, graphics, or sampled sound. See synthesis.md for the implemented
approximation and limits.

**gtracker_project** validates and serializes the editable document, converts
beat positions into sample timestamps, configures the core and writes WAV files.
Its native event snapshot is independent of the editor. Stable sorting preserves
source order for equal-time events. An offline render includes an explicit tail.

**gtracker_scripts** exposes coarse editing commands to embedded Lua. A command
runs against a copy; validation succeeds before the caller receives the new
document. The same operations can run in the CLI and GUI. The disabled-Lua build
returns a clear error for an explicit script request.

**gtracker_editor** uses SDL3 and Dear ImGui with a specialized tab canvas, plus
ImPlot for the waveform. The starter uses ImGui's maintained SDL_Renderer3 backend;
this also permits a dummy/software headless smoke. A GPU backend can be selected
later without changing the score or instrument. Rendering runs on a native worker
with a document snapshot. A generation counter rejects stale results even after
load/undo; playback queues only completed audio for the current generation.

**analysis/analyze.py** is a separate Python process. It currently reads PCM WAV
and produces truthful metadata-only results with job/revision provenance. Future
recognizers return proposals with confidence and source-sample coordinates. The
original take, microphone characteristics and analysis evidence belong to the
authoring project, not the shipped score. No model downloads are implicit.

## Changes to make deliberately

- Keep source data declarative and versioned. Lua bytecode is not the song format.
  Add a compact runtime encoding after the event vocabulary settles.
- Add guitar gestures as meaningful operations plus local curves. Standard tab
  markers are a presentation of those operations; continuous contact and pitch
  require data beyond a string/fret note pair.
- Model a 12-string guitar as six courses controlling paired physical strings.
  Today's 12 independent slots do not establish that behavior.
- Preserve source audio, pre-roll and decay tails. Musical insertion boundaries
  and audio analysis boundaries need not be identical. Latency calibration is
  separate from quantization of a player's timing.
- Keep device servicing light and sample-counted. Native physics and Python
  inference must stay outside time-critical audio servicing.
- Arrangement must render continuous instrument state. Do not independently
  render pattern WAVs and assume concatenation preserves ringing or reverb.
- A render cache must include canonical performance, instrument, room settings,
  seed, sample rate and renderer version. Checkpointing actual acoustic state is
  future work; a pattern boundary alone is not a valid state checkpoint.
- Python fitting should call this renderer, first through the CLI and later
  through a bulk native binding. A binding does not make the model differentiable;
  gradient-based synthesis fitting needs a separate derivative implementation.

## Research and evidence

Keep the present model as a selectable reference when adding contact physics,
string/body coupling, radiation and room production. Compare identical musical
inputs and seed with changes isolated by configuration. Report signal measurements,
actual render time and listening findings separately.

Short original synthetic fixtures are useful for transport, bounds and determinism
tests. They do not prove that real-guitar transcription or timbre is accurate.
Real takes and trained weights stay in ignored local storage; retain reproducible
capture/model/parameter provenance without uploading personal audio by default.

## Scope of the initial application

This is one native authoring tool and reusable instrument, not a DAW, plug-in host,
distributed render service or new general GUI framework. JUCE remains a possible
host if professional device/DAW integration becomes a concrete requirement; the
instrument boundary preserves that choice. No MMO game code is imported here.
