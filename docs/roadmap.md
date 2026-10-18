# Roadmap

The first musical goal is a two-bar take becoming an editable pattern and then
resynthesizing through the same native instrument. Full-song arrangement and the
complete vocabulary of guitar nuances remain part of the direction.

## Near-term tasks

1. [Qualify the native editor on Windows, Intel Mac and Linux](https://github.com/timosaarinen/gtracker/issues/1), including real
   playback and basic editing, and produce relocatable developer packages.
2. [Record short takes into a pattern](https://github.com/timosaarinen/gtracker/issues/2) with count-in, pre-roll, preserved tails,
   latency calibration and explicit sample-to-beat timing.
3. [Extend the performance format and tab interaction](https://github.com/timosaarinen/gtracker/issues/3) to continuous guitar
   gestures: slides, bends, vibrato, hammer-ons, pull-offs and contact/muting.
4. [Develop the physical instrument beyond the baseline](https://github.com/timosaarinen/gtracker/issues/4): excitation/contact state,
   string/body coupling, materials/dispersion and paired 12-string courses.
5. [Build audio-to-pattern research baselines](https://github.com/timosaarinen/gtracker/issues/5) and a small evaluation corpus, then
   recover increasingly rich gestures with editable uncertain proposals.
6. [Add song arrangement and continuous-state rendering](https://github.com/timosaarinen/gtracker/issues/6), loop boundaries and a
   renderer-versioned persistent cache suitable for game loading.
7. [Add algorithmic room reverb](https://github.com/timosaarinen/gtracker/issues/7), stereo production and reproducible listening
   comparisons without requiring a sampled sound bank.

These areas should land as small usable increments with configuration/CLI
controls. They are not a requirement to solve all transcription or acoustics
before the manual editor can produce useful music.

Future task details and acceptance criteria are tracked in the repository's
[GitHub issues](https://github.com/timosaarinen/gtracker/issues).
