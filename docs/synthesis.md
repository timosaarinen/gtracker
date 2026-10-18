# Synthesis baseline

Renderer version: **0.1.0**. The dependency-free library exposes the C interface
in `include/gtracker/core.h`. It is shared by the editor and command-line renderer;
it has no dependency on SDL, Lua, JSON, Python, an audio device, or sampled audio.

## What exists

The current instrument is a **Karplus–Strong-style plucked-string baseline**.
Each active string has its own seeded excitation and fractional delay line. A
two-tap low-pass filter and a feedback gain progressively remove energy. Linear
interpolation permits fractional periods; the delay approximately compensates
the low-frequency phase delay of the filter. Fret values may be fractional.

The requested pitch is `open_tuning_hz * 2^(fret / 12)`. The decay setting chooses
the nominal low-frequency feedback loss for a 60 dB amplitude reduction over that
time. Interpolation and filtering also remove energy, so actual harmonic decay
varies with pitch, brightness, and sample rate. This is not a calibrated acoustic
guitar model, and the decay control is not a promise of an exact measured T60.

`pluck` initializes the addressed string with new zero-mean pseudorandom
excitation and sets its fret. It replaces that string's previous vibration; other
strings continue ringing. A zero-strength pluck therefore clears that string.
`damp` immediately scales its vibration and adds loop loss until the next pluck.
A damping strength of one silences that string at the event's sample. Body
resonance can continue after a string is muted.

Six synthetic damped modes provide a small body contribution. String vibration
feeds these modes in one direction; a `body_hit` directly excites them. Their
frequencies and weights are illustrative constants, not measurements of a real
instrument. Setting `body_mix` to zero also makes body hits inaudible. The body
is not coupled back into the strings. There is no room reverb in this library yet.

The mixed signal passes through a gentle `tanh` output stage to keep dense chords
and repeated hits bounded in `[-1,1]`. That safeguard is not part of the physical
model. Audio is mono float PCM; stereo microphone placement and room production
are future work.

## Configuration and events

Defaults are 48 kHz, seed 42, and six strings in tab order: **E4, B3, G3, D3, A2,
E2**. The API uses zero-based string indices. The document layer may expose the
more familiar one-based numbering.

| Parameter | Accepted values |
| --- | --- |
| Sample rate | 8,000–192,000 Hz |
| String count | 1–12 independent strings |
| Active open-string tuning | 20–2,000 Hz and at most sample rate / 4 |
| Fret | 0–36, including fractional values |
| Plucked target frequency | At most sample rate / 4 |
| Decay | 0.02–60 seconds |
| Brightness, body mix, event strength | 0–1, finite |
| Event count | At most 1,000,000 per performance |
| Event timestamp | Unsigned 64-bit sample frame, except its maximum value |

Only active tuning slots are validated. Increasing `string_count` requires
supplying tunings for the extra strings. Twelve slots currently mean twelve
independent strings; six paired courses, octave/unison pairing, and shared
excitation are not implemented. `body_hit` ignores its string and fret fields;
`damp` ignores fret. All event float fields must nevertheless pass the documented
finite/range checks.

Events must be sorted by nondecreasing frame. Equal-frame events execute in input
order, before rendering that frame. Loading an event list copies it and rewinds
the renderer. Validation/allocation failure leaves the prior performance and
playback state intact. Reset preserves the copied events and configuration while
clearing all acoustic state, rewinding transport, and restoring the seed.

## Rendering contract

`gt_create` allocates bounded string buffers from the validated configuration.
`gt_set_events` allocates the copied performance. `gt_render` does not allocate;
it processes a caller-supplied sample buffer. The caller owns that buffer and must
serialize calls on a particular synth. Independent synth instances share no
mutable state.

The offline renderer advances one canonical sample clock. Rendering a performance
in one call or in arbitrary smaller blocks yields identical samples within the
same build. Reset yields the same result, including excitation noise. Standard
library transcendental functions and floating-point implementations can differ
between toolchains and platforms, so bit-identical cross-platform output is not
promised. Render-cache keys should include renderer version and configuration as
well as the complete performance and seed.

Long event-free passages continue advancing acoustic state. Pattern boundaries
do not reset ringing strings or body modes. Higher-level code decides when to
reset, how to arrange patterns, and how much tail to render.

## Deliberate limits and next research

The 0.2 native core also exposes an explicitly selected experimental
`contact_0_2` model through `gt_create_model`. `gt_create` always keeps
`baseline_0_1`. The original `gt_config`/`gt_event` layouts and entry points are
unchanged; new callers can use `gt_set_performance` with copied `gt_slide`
descriptors and relative-frame fret points. No C/C++ memory layout is a file
format. [Pattern v2 and native authoring](gestures.md) use the same model/API.

The contact prototype linearly interpolates fret points at native sample
resolution and changes a ringing string's delay length without replucking it.
It retains damping and the final fret. A contact curve alone makes no sound.
Same-string curves cannot overlap; conflicting plucks inside a curve are rejected.
At a curve's end, a same-frame pluck takes precedence. Reset/copy/partition
behavior remains deterministic within a build, with no render-time allocations.
With no curves the explicit contact model renders the same samples as baseline.

This moving-delay experiment is not a physical fret/finger collision model. It
can introduce modulation artifacts and energy loss while delay length changes.
Tests establish retained vibration, initial/final pitch, silence without
excitation, damping and transport behavior, not realistic slide timbre.

This baseline provides an audible, editable target for the first recording and
tab experiments. It does **not** implement physical fret/finger contact, continuous
slides or bends, hammer-ons, pull-offs, string collisions, pick or nail geometry,
sympathetic string coupling, calibrated radiation, microphone models, or different
body-strike locations. A pluck at a fractional fret is a static pitch choice; it
is not a continuous gesture.

The next synthesis work should introduce explicit contact and excitation state
while retaining this model as a selectable, reproducible reference. Models should
be evaluated with shared musical inputs and measured outputs. Tests check basic
signal behavior and transport correctness; they are not evidence of realistic
guitar timbre or a substitute for listening comparisons.
