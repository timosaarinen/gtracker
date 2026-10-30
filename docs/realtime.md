# Realtime playback

Play does not require a complete audio render. Guitar playback constructs the
same `NativeRenderer` used by exports and reads 512-frame blocks on a producer
thread. The queue is capped at roughly 2,048 frames ahead (about 43 ms at 48 kHz,
plus the current block and device buffering). No synthesis or IPC runs in the
SDL audio callback. The selected guitar audio output applies to auditions too.

SID playback uses a persistent external GPL helper with a small binary protocol.
It warms emulator analogue tables at startup, loads the genuine compiled SID,
and returns requested PCM blocks. The complete C64/player/oscillator state runs
continuously, including across pattern loops. It uses the same PAL/6581 settings
and interpolating resampler as offline export. Export quality is already the
high-quality emulator setting; playback does not substitute a simpler oscillator.

Notes, full-song playback and pattern loops use this service. Compilation is
asynchronous and bounded by the score, not by audio duration. Restarting an
unchanged song reuses its compiled SID. A new edit cancels stale preparation and
stops the previous snapshot. Audition plays the latest requested note. This is
snapshot playback, not live rewriting of an already-running C64 program.

Seeking preserves history by advancing the emulator from the beginning off the
UI thread; a distant seek can therefore take longer than starting at row zero.
Stopping cancels that work between blocks. A failed helper reports an error;
there is no silent fallback to a different synthesizer. The process is owned by
the editor and terminated when its instance is destroyed.

## Verification

`python tools/sid_stream_verify.py` compares the real helper's small PCM blocks
against a normal WAV export and repeats a load in the same process. On Windows
11 / i7-10700K, the measured helper startup/warm-up was about 327–450 ms; loading a
prepared SID and obtaining its first 512 frames then took about 13 ms. This is
software timing, excluding compilation, SDL/device buffers and physical latency.
After warm-up, the 520,343-frame fixture differed by at most 3 signed-16 units on warmed reloads;
the test permits at most 4. The engine's retained internal state means realtime
reloads are not claimed to be bit-identical to a fresh-process offline export.
Offline repeatability remains covered by the existing SID verification tool.

Native UI tests exercise synthesis start, pause/resume, stop, looping, stale jobs,
output routing and guitar production settings with dummy devices. Human listening
and real-device latency remain separate release qualification.
