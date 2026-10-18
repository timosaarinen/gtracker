# Continuous slides and pattern v2

The first continuous gesture is a slide: a local fret curve on one independent
string. It changes contact while vibration continues. A pluck supplies excitation;
a slide by itself is silent. This prototype uses a moving delay length, with
artifacts/energy loss possible during motion. It does not establish realistic
finger contact or guitar-slide timbre.

Try the original study:

```sh
python tools/dev.py render examples/slide-study.gtrk.json --output .local/slide-study.wav --json
python tools/dev.py editor examples/slide-study.gtrk.json
```

Click an amber slide line or `/` marker to inspect it. Edit its start beat,
string, and fret points; point beats are offsets from the start. `+ Point`
subdivides the largest interval. Remove points or delete the selected slide;
Delete and Undo/Redo also work. Numeric entries are grouped as one undo step.
The detailed beat controls preserve unsnapped values; changing the tab's snap
setting does not quantize existing curves. Save/reopen and CLI rendering use the
same shared native renderer.

For an existing v1 file, select **Enable slides** to explicitly choose the
experimental model and v2 format, then **+ Slide** on a string lane. The new
curve starts from the latest authored contact/pluck and ends before the next
same-string pluck/curve. It retains its stable ID as it is edited or undone.
The instrument control offers the original baseline model; remove slide data
before selecting it. Curves are never discarded to make baseline rendering fit.

A pluck exactly at a slide's start must share its first fret. In the editor,
editing either of these shared initial frets updates both in the same undoable
operation. Later plucks and contact points remain separate. Plucks strictly
inside an active slide are currently rejected; a pluck at its end takes
precedence. Damping works during motion and the final fret persists until a
later pluck/curve changes it. Same-string slides can touch but cannot overlap.

## Explicit format behavior

- **v1** retains its original strict fields, implicit `baseline_0_1`, static
  plucks/damps/body hits and original render samples. Loading/saving v1 does not
  silently migrate it. The original campfire WAV remains byte-identical.
- **v2** requires `instrument.model` and `gestures`, even when that array is
  empty. Models are `baseline_0_1` (no gestures) or `contact_0_2`. A v2 contact
  pattern with no curves currently produces the same samples as baseline.
- Only `slide` with piecewise-linear fret points is implemented. Future schema
  versions, models, gesture types, interpolation fields or unknown properties
  fail validation. No curve is flattened to MIDI/static notes or silently dropped.
- IDs are unique across events and gestures. Each slide has 2–256 points, starts
  at offset zero, and has strictly increasing offsets. There are at most 16,384
  gestures and 65,536 points per pattern, within the 1 MiB document limit.

```json
{
  "id": "slide-1", "type": "slide", "beat": 0.25, "string": 6,
  "points": [
    {"beat": 0, "fret": 0},
    {"beat": 0.333333333333, "fret": 2.5},
    {"beat": 1.25, "fret": 7}
  ]
}
```

Beat values use double precision; fret values use the native float precision of
the existing instrument. Save/load preserves the resulting local curves and
fractional frets. Rendering rounds each absolute control time to a native frame.
If two points collapse to one frame, the render fails with a useful message;
increase the rate or separate the points. The saved timing is never rewritten.
Only a tiny 1e-9-beat tolerance accommodates floating-point boundary arithmetic;
this is far smaller than a frame at supported rates/tempos.

Preserved-take alignment remains in its original source-sample coordinates.
Editing a model/curve advances the pattern revision and leaves the original
source and metadata-only analysis untouched; older analysis is shown as older.
No uncertain note/technique proposals are generated or imported yet. Six/seven/
twelve-string instruments still use independent slots, without course mapping.

## Lua and CLI authoring

```sh
.local/build/headless/bin/gtracker_cli transform examples/campfire.gtrk.json \
  --script scripts/slide.lua --output .local/campfire-slide.gtrk.json --json
```

`gtracker.set_model("contact_0_2")` explicitly selects v2 before
`gtracker.add_slide(beat, one_based_string, {{beat=0,fret=0}, ...})`.
Transposition edits plucks and every curve fret together while retaining IDs and
point timing. A failed model/curve/script transaction leaves the original
document intact. Lua remains trusted coarse authoring code; DSP is native.

The C ABI's `gt_create` stays on baseline. `gt_create_model` and
`gt_set_performance` copy validated slide arrays into the core; pointers and raw
struct memory are not serialized. The native tests cover retained vibration,
pitch, damping, sample timing, partitioning, atomic rejection, versioned round
trips, unsnapped points, Lua and actual widgets under dummy SDL. Physical window
interaction and listening are separate evidence.
