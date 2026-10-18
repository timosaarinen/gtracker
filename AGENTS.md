# Agent working rules

Read `README.md`, `docs/handoff.md`, and the relevant issue before editing. The
repository and the user's latest decisions are the source of truth. This is
**gtracker**, an acoustic-guitar tracker and offline physical-synthesis project
for Lust of War and standalone music authoring.

- KISS. Land small usable changes on `main`; use short-lived branches or separate
  worktrees when they help isolate work. Commit and push finished, verified work
  when the user has asked for repository changes. Never force-push published
  history, reset someone else's work, or change repository visibility.
- Keep the C++ instrument core independent of SDL, ImGui, Lua, Python and the
  editor. The CLI, editor, analyzer experiments and game use the same renderer.
- Lua owns coarse musical/editor operations. Dense drawing and DSP stay native.
  Python analysis is an authoring dependency and communicates through versioned
  jobs/results. Preserve source audio and uncertainty; never claim a baseline
  recognizes articulations it does not implement.
- Short recorded takes become patterns; full-song support uses the same musical
  representation. Six/seven/twelve-string guitars, courses, contacts, continuous
  expression and body percussion are the long-term direction. Check the current
  schema and implementation before claiming support.
- Run `python tools/dev.py doctor` and `python tools/dev.py ci-status` on startup.
  During active work, run `python tools/dev.py ci --if-due` when due. Local checks
  are the verification authority. Hosted Actions are manual cross-platform
  supplements; no background scheduler or agent wake-up is implied.
- Run focused checks after changes. Use the full suite to resolve a concrete risk
  or when due, rather than rerunning everything for every edit. A previous pass
  does not certify changed code. Record commands, failures, skipped capabilities,
  tested source revision and platform honestly.
- Native compilation, headless UI smoke, real window interaction, listening and
  microphone capture are separate evidence. Never substitute one for another.
  Respect the current environment's GUI restrictions; report blocked checks.
- Build outputs, recordings, WAV renders, model weights, caches, local logs and
  private machine configuration belong under ignored `.local/`. Commit small
  original text fixtures; generate audio fixtures during tests. Do not commit
  credentials, personal recordings, downloaded weights or third-party binaries.
- Keep experimental behavior selectable through files/CLI overrides. Changes to
  sound need repeatable inputs, parameters, seed and renderer version. Report
  measured performance with the machine and workload, not an inferred speedup.
- Give collaborating agents explicit ownership of disjoint files, or separate
  worktrees. Reviewers remain read-only until reassigned. Use native agent
  messaging when available; local persistent notes can live in
  `.local/agents/chat.md`. Do not operate another agent's processes or commit its
  unfinished files. Fetch and inspect the remote head before publishing.
- Review the staged diff and run `python tools/repo_guard.py` before publishing.
  Fix published mistakes with new commits. Leave a concise handoff with what
  changed, what was actually checked, remaining limits and the next useful step.
