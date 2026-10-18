gtracker developer package

This is a development build, not a qualified public release. The project has
not selected its own distribution license. See THIRD_PARTY_NOTICES.md and the
licenses directory for dependency notices; they do not license gtracker itself.

Extract the whole gtracker folder. A headless package contains the CLI only.
In a desktop package, on Windows double-click gtracker_editor.exe; on Linux/macOS
run ./gtracker_editor. The editor opens an original built-in
sketch. Render + Play generates its audio. See docs/editor.md for controls.
The original slide-study example opts into the experimental contact model and
v2 continuous curves; see docs/gestures.md. V1 keeps the unchanged baseline.
The repluck-study and scripts/retain.lua compare explicit retained excitation;
see docs/retained.md for the experimental model and repeatable measurements.

Scripts are found beside the executable, regardless of the working directory.
With no explicit document, the suggested save location is the per-user gtracker
application-data folder, outside the installation. Use --data-dir PATH to choose
another writable directory. Save editable copies there when opening bundled
examples from a read-only installation. --resource-dir PATH overrides resources.
Open a preserved take with --take PATH/TO/take.json to compare its source audio
with the native synth. Source playback and hash verification need no Python.
See docs/takes.md for the optional Python import workflow in a source checkout.
Desktop packages also include gtracker_capture for explicit device/file capture,
nominal sample timing and partial-take recovery. See docs/capture.md before use.

Command-line examples (use .exe on Windows):

  ./gtracker_cli validate examples/campfire.gtrk.json --json
  ./gtracker_cli render examples/campfire.gtrk.json --output /writable/path/demo.wav --json
  ./gtracker_cli render-song examples/night-shift.gsong.json --cache-dir /writable/path/cache --output /writable/path/night-shift.wav --json
  ./gtracker_cli --version --json
  ./gtracker_editor --data-dir /writable/path

This package includes native tools, original patterns/song, Lua examples, schema,
controls/model documentation and notices. It contains no recordings or weights.
An optional Python research worker is not required to render/play authored music.

Platform runtime libraries are prerequisites, not bundled here. Windows builds
using MSVC/Clang require the Microsoft Visual C++ 2015-2022 x64 runtime. Linux
desktop builds require the system windowing/audio libraries used by their build.
The package manifest records native build identity, source fingerprint and build platform; that is
provenance, not evidence that another operating system or physical device passed.

Patterns, script filenames and CLI JSON use UTF-8. The bundled UI font has limited
glyph coverage. Microphone hardware and latency require separate qualification;
the generated/dummy capture checks do not establish them. No transcript exists.
