# Contributing

Start with the README and the guide for the subsystem you want to change.
The project license is pending; contact the owner before contributing code
whose licensing would depend on a project-wide license grant.

## Development

Use `bun run dev` for the native editor and `python tools/dev.py doctor` to check
prerequisites. C++ owns DSP and dense UI work; Lua owns coarse musical operations.
Python is an optional authoring/build dependency, not an audio callback runtime.
Keep the instrument core independent of SDL, ImGui, Lua and Python.

Make small changes with clear behavior and focused tests. Run the relevant
native/Python tests after editing. Use `python tools/dev.py ci --if-due` for the
local cadence and full CI when a change spans core boundaries. Record the actual
commands, source revision, platform, failures and skipped capabilities.

## Reports and pull requests

Describe the concrete trigger, expected behavior and observed result. Include a
small original text score where possible. Distinguish a compiled test, simulated
UI input, a real-device observation and a listening judgment. Do not submit
private recordings, credentials, generated media or downloaded dependencies.
Generated artifacts belong under ignored `.local/`.

Keep sound changes selectable and reproducible: source score, seed, model,
parameters and renderer version must identify the result. Preserve source audio
and uncertainty in analysis workflows. Avoid claims unsupported by measurements.

Before committing, inspect the staged diff and run `python tools/repo_guard.py`.
See AGENTS.md and docs/workflow.md for repository-specific working rules.
