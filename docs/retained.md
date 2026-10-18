# Retained excitation experiment

Renderer 0.5 adds the explicit `retained_0_5` instrument model. It keeps part of
an already ringing string when the next pluck excites it. The original
`baseline_0_1` and `contact_0_2` remain selectable; loading old documents does not
change their sound. This is a small synthesis experiment, not a claim of
realistic pick, finger, fret or string collision physics.

For an active string, a pluck keeps the delay's current read/write phase, scales
its existing vibration and filter memory by `1 - 0.5 * strength`, then adds the
same seeded, zero-mean noise excitation used by the older models. A first pluck
on an inactive string uses the original initialization. Retained excitation has
one preallocated scratch buffer per string and no render-time allocation.
Pitch changes still use the experimental moving delay from `contact_0_2`.
Damping and continuous slides use the existing native event/curve API.

A zero-strength retained pluck updates contact and restores normal loop loss
without adding excitation or clearing vibration. Use `damp` with strength one
to mute. In the older models a zero-strength pluck still clears that string.
The three models share their original loss filter, synthetic body modes and
bounded output stage; no measured guitar response or string coupling is added.

## Play an A/B study

The original repluck study contains repeated notes, changed contacts and explicit
damping. The first command renders the existing replacement model. The Lua
transaction changes only the selected model, retaining the musical events.

```sh
python tools/dev.py render examples/repluck-study.gtrk.json --output .local/repluck-contact.wav --json
.local/build/headless/bin/gtracker_cli transform examples/repluck-study.gtrk.json --script scripts/retain.lua --output .local/repluck-retained.gtrk.json --json
python tools/dev.py render .local/repluck-retained.gtrk.json --output .local/repluck-retained.wav --json
```

Choose **Retained plucks 0.5** in the editor's instrument control to make the same
explicit, undoable choice. Both experimental models support saved v2 slides.
The model is part of pattern/song compatibility checks and render-cache identity.
`gt_create` keeps the baseline; native callers use
`gt_create_model(..., GT_MODEL_RETAINED_0_5, ...)`.

## Numeric tails and measured cost

Only the new model clears double-precision subnormal string/body state to zero
below the smallest normal double (`DBL_MIN`, approximately `2.225e-308`). It does
not change the caller's floating-point control registers. The older models retain
their previous numeric path. Subnormal arithmetic can be costly on some machines;
see Intel's [discussion of denormal arithmetic](https://www.intel.com/content/www/us/en/docs/onemkl/developer-guide-windows/2023-2/operating-on-denormals.html).

An isolated, original body tap followed by 98 seconds of decay at 16 kHz was
measured on Windows 11, Intel i7-10700K (8 cores/16 logical processors), Clang
18.1.8 Release. Native generation took 872.48 ms in the 0.4 baseline, 871.85 ms
in the 0.5 baseline, and 27.13 ms with the retained model. All three PCM16 files
had the same SHA-256. This workload has no plucks, isolating numeric tail handling
from the new excitation semantics. The result is consistent with subnormal cost;
no hardware-counter profiling or general speedup claim is made.

A separate sparse 770-second score took 2,118.53 ms of native generation with
the retained model. Replucks change that signal, so comparison with a baseline
render is not an equal-signal performance comparison. Reports under ignored
`.local/nightshift/retained-measurements/` record the commands, parameters, build
identity, source fingerprints, WAV hashes and separate process/export timing.

## Repeatable known-score probes

```sh
python tools/model_probe.py --machine-label "My CPU / compiler / build" --json
```

This optional standard-library tool renders isolated original single-string
fixtures through the native CLI. It records 18 cases: all three models, open
MIDI tunings 40/52/64, and frets 0/7. The default is 16 kHz, seed 42, brightness
0.78, nominal decay four seconds, body contribution zero and a five-second dry
render. Each run keeps unique inputs, audio, hashes and a source-bound report
under `.local/`.

Pitch is estimated from 200–500 ms using autocorrelation conditioned on the
declared expected frequency within ten percent. It excludes octave alternatives
and rejects low-correlation/boundary results. It is a model measurement tool,
not a transcription algorithm. A separate metric finds three consecutive 50 ms
RMS windows at least 20 dB below the 200–250 ms reference. It is not a broadband
T60 measurement, and PCM16 limits very quiet measurements.

The Windows run found a maximum absolute pitch error of 0.1374 cents in those
18 conditioned cases. Their RMS drop times were 0.65–1.15 seconds despite the
four-second nominal low-frequency loop decay. Initial plucks matched across
models, as expected. These checks establish repeatability and basic pitch/decay
behavior for those fixtures. They do not establish realistic guitar timbre,
physical decay calibration, cross-platform bit identity or a listening verdict.
