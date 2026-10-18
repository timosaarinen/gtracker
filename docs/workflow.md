# Development and agent workflow

Use Python 3.10 or newer for the command runner, CMake 3.25 or newer, Ninja, and a
C++ toolchain. The native dependency revisions are defined in CMake. Initial
configuration needs access to those dependency sources; subsequent local builds
can reuse the downloaded sources under `.local/`.

On Windows, start in a Visual Studio developer terminal so Ninja can locate MSVC
and the Windows SDK. Linux desktop builds need the SDL windowing/audio development
packages listed in `.github/workflows/qualify.yml`. macOS needs the Xcode command
line tools and CMake/Ninja. The `headless` preset does not build the desktop UI.

## Start a working session

Read `AGENTS.md`, `README.md`, `docs/handoff.md`, and the relevant issue. Then run:

```sh
python tools/dev.py doctor
python tools/dev.py ci-status
python tools/dev.py ci --if-due
```

`doctor` reports tools without downloading dependencies or claiming an audio/GUI
test. It reports missing tools as a failure; CMake configuration checks the actual
compiler and dependency requirements. `ci-status --json` is available for agents.

`ci --if-due` runs when there is no passing receipt, the latest attempt failed,
the evidence is corrupt/foreign, or the latest result is at least 24 hours old.
Ordinary source edits do not by themselves trigger another full run. The separate
`source_matches` field and qualification text tell whether the latest pass covers
the current bytes: **not due does not mean changed code has been verified**.
Run focused checks after edits, and a full run when it resolves a concrete risk or
the cadence requires it. This is a command agents invoke during active work, not a
scheduler or agent wake-up mechanism. `ci` without `--if-due` always requests a run.

## Build and use the prototype

All commands resolve project-relative paths from the repository root, even when
the runner is invoked by absolute path from another directory.

```sh
python tools/dev.py configure
python tools/dev.py build
python tools/dev.py test
python tools/dev.py render --output .local/demo.wav --json

python tools/dev.py configure --profile desktop
python tools/dev.py build --profile desktop
python tools/dev.py editor examples/campfire.gtrk.json
```

The default profile is `headless`, with Lua enabled and the editor disabled. The
`desktop` profile also enables the editor. Both use Release configuration. Native
binaries are under `.local/build/<profile>/bin/`.

`render` forwards `--sample-rate`, `--tail`, and `--script` to the native CLI.
`editor` forwards `--script`, `--smoke-test`, `--frames`, and `--screenshot`.
A smoke run is separate evidence from a real interactive window or listening.
The native CLI also exposes `validate FILE --json` for direct use by agents.

Focused Python checks can run without compiling or installing research packages:

```sh
python -m unittest discover -s tests -p 'test_*.py' -v
```

The full local CI command configures/builds/tests the headless preset, runs these
Python tests, and renders `examples/campfire.gtrk.json` to `.local/demo.wav`.
It stops at the first failed command and retains its real return code and log.

## Developer packages

`python tools/dev.py package --profile desktop` incrementally builds the configured
profile and writes `.local/packages/gtracker-<profile>-<system>-<architecture>.zip`. Use
`--profile headless` for CLI-only packaging or `--output .local/PATH.zip` to choose
another ignored location. Packages include resources, notices and a manifest
with per-file SHA-256, source revision/dirty state/fingerprint, and build platform.
A failed build or a source change during assembly leaves any previous ZIP intact.
These are local developer bundles; no release is created and no repository
visibility or distribution license is changed. Native system runtimes remain
prerequisites; see the packaged README.

The editor's `--data-dir` and `--resource-dir` separate writable work from bundled
resources. The developer runner supplies `.local/` and the repository root; direct
packaged startup uses SDL's per-user data folder and the executable directory.

## What the CI receipt proves

Each attempt writes `.local/ci/runs/<UTC-and-id>/report.json`, one log per executed
command, and `.local/ci/latest.json`. Receipts include exact argument arrays,
return codes, timestamps, runtime platform, Git revision/dirty state, and a SHA-256
fingerprint of tracked and nonignored untracked working-tree source paths and
bytes. They work before the initial commit too. Generated outputs remain ignored.
The receipt also records a hash binding it to the checkout path, hostname,
operating system, and architecture; it does not read an OS machine identifier.

The runner fingerprints source before and after the run. A change during the run
prevents it from reporting a current pass, even if every command exited zero.
Committing identical already-tested file content does not alter the fingerprint;
the receipt still reports the Git revision that existed when it ran. A receipt
copied from another checkout or host is foreign evidence and makes the local run
due. Source changes after a pass are reported separately from the 24-hour cadence.

Only one local full CI process may own `.local/ci/lock.json`. Ownership is created
with exclusive file creation. An existing live or unverifiable owner blocks a new
run. A same-host PID that is confirmed absent is reported as a probable stale lock;
the runner deliberately does not remove it or operate another process. Inspect
the evidence and confirm no CI run remains before removing such a lock. Successful
exit and handled failures release only the lock carrying that runner's own token.

Local headless CI does **not** prove desktop compilation, GUI interaction, sound
quality, microphone capture, or behavior on another OS. Record those checks
separately when they are performed. No current analyzer transcription or physical
model fidelity should be inferred from a green build.

## Hosted qualification

The `Manual cross-platform qualification` workflow is `workflow_dispatch` only.
It supplements local checks on Linux, Windows, and an Intel macOS runner, running
the local headless suite, compiling/testing the desktop target with dummy video/
audio, and assembling a developer ZIP. Its artifact contains local CI logs, UI
test images, the example render and that developer bundle. Dummy checks do not
qualify a physical display, output device or microphone. There are no
automatic push/PR jobs, scheduled jobs, or mandatory installed Git hooks.

## Publish a small, finished change

Keep recordings, model weights, downloaded binaries, generated audio, machine
configuration, logs, and builds in ignored `.local/`. Commit small original text
fixtures and generate audio fixtures in tests.

Stage the intended files, review `git diff --cached`, and run:

```sh
python tools/repo_guard.py
```

The guard examines **index blobs**, so an unstaged cleanup cannot hide a staged
credential or artifact. It rejects generated/private paths, audio/model/binary
artifacts, known credential patterns, and blobs larger than 2 MiB. It withholds
matched credential values. It is a focused repository-hygiene check, not a claim
to recognize every secret. Resolve a rejection deliberately rather than treating
the guard as a reason to replace or bypass the user's publishing authorization.

Fetch and inspect the remote before publishing. Prefer small finished changes on
`main`; use a short-lived branch or separate worktree when it makes coordination
easier. Never overwrite another agent's work or force-push published history.
Leave a concise handoff stating what changed, what was actually checked, what is
still limited, and the next useful step.
