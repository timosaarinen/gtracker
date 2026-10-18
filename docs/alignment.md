# Relative delay and loopback references

`align-audio` compares a known reference with a recording. It reports a bounded
**relative file delay**, with source hashes, selected channels, scores and the
search range. Positive frames mean the recorded waveform appears later than the
reference waveform. Neither file is rewritten, resampled or quantized.

```sh
python tools/dev.py calibration-probe --output .local/probe.wav --sample-rate 48000 --seed 42 --json
python tools/dev.py align-audio --reference .local/probe.wav --recorded .local/recorded-probe.wav \
  --max-offset-ms 500 --recorded-channel 1 --output .local/offset.json --json
```

The native `gtracker_cli` exposes the same commands without Python. The generated
probe is an original deterministic two-second PCM16 mono signal: five irregularly
spaced, faded broadband bursts at a peak bound below 0.123 full scale. Its seed,
rate and hash are reported. Generation opens no audio devices. Keep probe files
and recordings under ignored `.local/` in a source checkout.

## Routed loopback workflow

Route the selected output back to the selected input and choose that input
explicitly. The capture tool can queue the probe in the input stream's nominal
timeline while retaining the input, reference and surrounding context:

```sh
python tools/dev.py record-take --pattern examples/campfire.gtrk.json \
  --device 0 --output .local/takes/loopback --count-in 4 --pre-roll 1 --tail 1 \
  --calibration-probe --json
python tools/dev.py align-audio --reference .local/takes/loopback/reference.wav \
  --recorded .local/takes/loopback/source.wav --max-offset-ms 500 \
  --output .local/loopback-offset.json --json
```

`--calibration-probe` replaces the regular count-in clicks with the broadband
probe; the count-in span must be at least two seconds. The pattern seed selects
the reference. The exact reference WAV and hash are saved, and the receipt labels
its kind/seed. This optional metadata extends capture v1; earlier receipts omit
the fields and contain regular count-in references. `--silent` retains the
reference without playing it, which is useful for generated tests.

A match measures the difference between the **two file timelines**. In the
routed capture this includes output buffering, input buffering, routing and
application/device start offset. It does not separate input latency from output
latency, establish a shared hardware clock or measure drift. Absolute clock
alignment cannot be inferred from equal rates or SDL sample-rate conversion.
File origins must be understood before using the result as a musical offset.

The estimator never changes a take or marks it measured/calibrated. To author a
manual offset, import a new bundle using the preserved `source.wav` and pattern,
the receipt's `pattern_zero_frame`, and an explicit `--input-offset-frames N`.
The take reports `manual`; the capture receipt remains `uncalibrated`. Use
repeated real loopback trials to investigate consistency. No real-device trial
is claimed by generated or dummy checks, and issue #2 remains open for it.

## Bounds and interpretation

- Both files must have the same 8–192 kHz sample rate; mono/stereo channels are
  selected explicitly with one-based `--reference-channel`/`--recorded-channel`.
- Search is limited to ±1–2000 ms, default ±500 ms. It uses the first 0.1–5 seconds
  of reference audio, default up to five seconds, with at least 80% overlap.
- Coarse matching uses roughly 1 ms mean-absolute envelopes and a centered,
  normalized correlation. A high-energy anchor of at most 2048 samples refines
  the best neighborhood at integer source-frame resolution. Polarity inversion
  is supported and reported. Source audio is unchanged throughout.
- Current match gates require coarse/fine coefficients at least 0.6 and a
  coarse-score margin of 0.08 over alternatives more than 20 ms away. Scores
  describe this bounded comparison; they are not probabilities or guarantees
  of physical timing accuracy. A single anchor does not measure clock drift.
- A `matched` result exits 0. `no_match`, `no_signal`, `ambiguous`,
  `insufficient_overlap` or `search_boundary` exit 1 and have a null applicable
  offset. The diagnostic candidate may still be reported. Invalid inputs exit 2.
  `no_signal` means the selected analysis window falls below a centered RMS
  threshold of 0.0001; it is not a musical-transcription judgment. Repeated
  references can produce several plausible lags and should remain ambiguous.

The core lag convention follows the sample cross-correlation definition in
[Julius O. Smith III, Mathematics of the DFT](https://www.dsprelated.com/freebooks/mdft/Cross_Correlation.html).
The bounded envelope/anchor search and acceptance thresholds above are this
prototype's original implementation, not a statistical confidence calibration.

Generated tests cover signed known delays at 8/44.1/48/192 kHz, mono/stereo,
gain/DC/polarity changes, different burst waveforms, silence, repeated-lag
ambiguity, search boundaries, source preservation and capture-to-take comparison.
Real cables, monitoring, microphone paths and long-term clock drift still need
separate measurements.
