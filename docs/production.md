# Native production and stereo room

The 0.3 renderer adds `room_v1`, an original algorithmic room effect after the
mono instrument. It needs no impulse-response bank, recorded samples or plug-ins.
This is an artistic space control, not a measured guitar body or room geometry.
Instrument controls and performance gestures retain their separate meanings.

## Render the same phrase dry and wet

```sh
python tools/dev.py render --output .local/campfire-dry.wav --json
python tools/dev.py render --production examples/room.json --output .local/campfire-room.wav --json
python tools/dev.py render --production examples/room.json --bypass-production --output .local/campfire-bypass.wav --json
```

The last command bypasses the entire production chain, including output gain,
and produces the exact original mono dry reference. Enabling production exports
interleaved stereo PCM16; source-take playback always uses the preserved original
audio. No automatic normalization or limiter changes the comparison.

The CLI also reads production settings saved in a v2 pattern. `--production`
temporarily overrides those settings without rewriting the pattern. The optional
top-level `production` object has the same fields as the standalone file, checked
against `schema/production.schema.json` and native validation. All fields are
required when the object exists; unknown algorithms, versions and fields fail.
V1 files stay v1 and dry. V2 files without production retain that absence.

In the editor, expand **Production / room**. Changing a control explicitly stores
v2 settings, supports grouped undo/redo and save/reopen, and makes an older render
stale. **Room enabled** switches the entire chain on/off. Render again to hear
the change. The shared waveform fits both stereo channels and their tail; its
display auto scale does not change audio gain.

## Controls and gain

| Control | Range | Meaning |
| --- | --- | --- |
| Wet | 0–1 | Linear dry/wet balance; 0.18 is the restrained starting setting |
| Decay | 0.1–8 s | Low-frequency comb-feedback target T60 |
| Damping | 0–0.95 | One-pole loss in the feedback; higher values shorten bright reflections |
| Pre-delay | 0–200 ms | Delay before feeding the room |
| Width | 0–1 | Wet mid/side width; zero gives identical channels |
| Output gain | −24–+6 dB | Gain after the active wet/dry mix |

The feedback gains depend on the actual rounded delay lengths and sample rate.
Four parallel filtered combs per channel feed two true allpass diffusers; left
and right use distinct delay lengths. Each comb's input scales by `1-feedback`
to bound its DC gain. Delay memory and filter state persist across processing
blocks. `reset()` explicitly clears them. This follows the established
[Schroeder topology](https://www.dsprelated.com/freebooks/pasp/Schroeder_Reverberators.html)
and [one-pole feedback damping](https://ccrma.stanford.edu/~jos/pasp/Lowpass_Feedback_Comb_Filter.html)
described by Julius O. Smith; the implementation and delay tuning are original.
There is no claim that the nominal decay equals a measured broadband room T60.

The instrument's existing output safeguard is unchanged. Production has no
additional limiter: an aggressive gain/mix can clip PCM16 export. The CLI reports
float peak/RMS and `clipped_samples` (samples at or beyond full scale) before
export clipping. Lower output gain when this count is nonzero. The editor render
status reports the same metrics and actual mono/stereo format.

## Tails, identity and limits

The existing `--tail` is the instrument's explicitly rendered decay after the
musical pattern. Enabled wet production then receives zero input for an extra
`pre-delay + 1.5 * decay + 0.15` seconds. This allows a nominal 90 dB feedback
decay plus diffuser context. `end_peak_last_50ms` makes the actual ending visible;
it is measured, not a guarantee for every possible input. With wet zero there is
no added room tail. Bypass adds no room tail and stays mono.

`render_identity_sha256` hashes versioned canonical JSON containing the full
pattern, seed, model, gestures, every production field, renderer version, rate,
instrument tail, native build identity, override and bypass choice. Names/revisions are included too,
so it conservatively invalidates for some changes that do not alter sound. No
compiler padding or raw C structs enter the identity. It is an identity for
the optional [persistent audio cache](cache.md); it is not a state checkpoint.

Offline pattern output is bounded to 32 Mi float samples across all channels.
The native processor allocates its delay memory at construction, not in
`process()`. It can retain room state across future arrangement/loop boundaries;
song scheduling and continuous transport are separate work and must reuse that
same instance rather than concatenate completed pattern WAVs.

Native checks cover reset/block partitioning, mono width, distinct stereo,
pre-delay, silence decay, longest feedback settings, finite output and bypass.
Document/CLI/UI checks cover settings, undo, reopen, deterministic PCM, exact dry
comparison and export format. These signal and dummy-device checks do not supply
physical playback or listening-quality observations.
