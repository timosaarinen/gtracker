# Short-take capture

The desktop build provides `gtracker_capture`, a separate SDL3 I/O tool. It uses
the same document/take code as the editor and keeps SDL out of the instrument.
Capture requires an explicit input choice; help and offline rendering never open
a microphone. All repository recordings belong under ignored `.local/`.

In a source checkout, use the authoring command to capture and create a complete
take bundle for source/synth comparison:

```sh
python tools/dev.py input-devices --json
python tools/dev.py record-take --pattern examples/campfire.gtrk.json \
  --device 0 --output .local/takes/first --count-in 4 --pre-roll 1 --tail 2 --json
python tools/dev.py editor --take .local/takes/first/take.json
```

`record-take` needs the desktop build and Python, invokes the native capture
tool, then runs the existing **metadata-only** analysis job. It checks source,
reference and pattern hashes plus job/revision/frame identity before publishing
the new bundle. Authored events remain unchanged. Each operation gets a unique
job ID unless `--job-id` is explicitly supplied. Editing the original pattern
while recording does not alter the active snapshot or its analysis revision.
The complete bundle includes the unmodified capture receipt and reference WAV
beside the normal v1 imported-take files; its origin is still `import` because
it imports the native application's capture without changing the v1 contract.

On cancellation, native failure or analysis failure, the command prints a
`recovery_path` under `.local/` and leaves the received WAV, pattern snapshot,
capture receipt and diagnostics there. No successful take is published. It
requests graceful cancellation before considering process termination. Retry
analysis using `import-take` on the retained `raw/source.wav` and
`raw/pattern.gtrk.json`, with a source-frame zero chosen from the receipt (or
bounded to the partial take); original recovery files remain available.

The lower-level packaged native tool works without Python:

```sh
.local/build/desktop/bin/gtracker_capture devices --json
.local/build/desktop/bin/gtracker_capture record \
  --pattern examples/campfire.gtrk.json --device 0 \
  --output .local/my-first-capture --count-in 4 --pre-roll 1 --tail 2 --json
```

On Windows the executable has the `.exe` suffix. Choose the current zero-based
device index, an unambiguous exact device name, or explicitly `--device default`.
Indices and SDL runtime IDs are not durable device identities. A device's
reported service-buffer size is not a measured input latency.

By default the application retains the device's recommended sample rate and
mono/stereo channel count. `--sample-rate 44100` and `--channels 1|2` explicitly
request application conversion. A multichannel input needs an explicit channel
choice. The application records interleaved signed PCM16 from the SDL stream,
stores those received bits exactly, and reports the actual opened device format
and whether SDL conversion is involved. This is the original **application
capture**, not a claim to preserve the hardware ADC's raw/unprocessed format.
The existing import command preserves external 8/16/24/32-bit integer WAVs.

The new output directory contains:

- `source.wav`: all received frames, including pre-roll, count-in and decay tail.
- `pattern.gtrk.json`: the exact validated pattern snapshot used to plan capture.
- `reference.wav`: the generated mono count-in/metronome reference and silence.
- `capture.json`: versioned status, hashes, format provenance and nominal timing.

The pattern's length and tempo define the take length. `--pre-roll` accepts
0–10 seconds, `--count-in` 0–16 beats, and `--tail` 0–30 seconds. The total is
bounded to 120 seconds and 32 Mi interleaved samples. Count-in playback is on
when requested; `--metronome` also clicks during the phrase. Use `--silent` to
capture without opening an output device. The reference file is retained in
either case. The metronome is a timing cue, not part of the guitar renderer.

Nominal beat zero is `pre_roll_frames + count_in_frames` in the application
input stream. Input/output device start offset, buffering latency and clock
drift are **uncalibrated**. This mapping neither measures latency nor quantizes
the performance. Sample-rate conversion does not establish clock alignment.
Real loopback/input-offset calibration remains a separate qualification step.

Ctrl+C, a `--cancel-file PATH` appearing, input removal/reconfiguration, and
stream failures stop servicing. Already received samples are written as a
partial source with an incomplete receipt. Exit codes are 0 for complete, 130
for cancelled, 1 for incomplete/runtime capture failure and 2 for usage or
output-write errors. The requested directory must be new and its parent must
exist. No existing take is overwritten. A forced process kill or storage failure
cannot guarantee recovery because file writes occur after capture servicing.

SDL services its audio stream; the application polls and copies preallocated
PCM buffers on its caller thread. WAV writing, hashes and analysis run after
servicing ends. No physics or Python inference runs in an audio callback. A
five-second no-progress timeout and a duration-plus-ten-second wall deadline
bound stalled servicing. For the default-device selector, a recording-device
removal/reconfiguration conservatively ends capture rather than silently moving
the performance to another input.

## Generated input checks

```sh
.local/build/desktop/bin/gtracker_capture record \
  --pattern examples/campfire.gtrk.json --simulate .local/long-generated.wav \
  --output .local/simulated-capture --count-in 0 --pre-roll 0 --tail 0 --json
```

File simulation opens no SDL device and never plays the reference. It retains
the file's rate/channels and requires enough input frames for the planned take.
PCM16 samples are copied exactly; other integer widths explicitly convert to
the application PCM16 format, leaving the input file untouched. It is tagged
`file_simulation`, including its input hash. `--realtime-simulation` paces file
copying; `--simulate-cancel-frame` and `--simulate-disconnect-frame` exercise
recovery. Cancellation is checked per block, not advertised as sample-exact.
SDL's dummy/disk drivers are separately tagged `sdl_simulated_driver`.

`capture_plan` and `capture_generated` test frame planning/bounds, 44.1/48 kHz
mono/stereo files, extrema, reference metadata, missing devices, cancelled and
disconnected partials, source exhaustion and dummy SDL input. These provide no
evidence of microphone quality, physical monitoring, hardware latency or real
unplug behavior. Issue #2 remains open until separately recorded real-device
and calibration evidence exists.
