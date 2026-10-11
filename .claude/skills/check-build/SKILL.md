---
name: check-build
description: Internal — called by prd-autopilot after each PRD, and by plan-pilot after a "skill:" step. Runs the firmware build and the format check (the net) and drives every failure to green, one at a time.
---

# check-build

The host tests are the dev loop: gtest suites under `test/`, seconds to a minute. The net is what CI checks and the host tests cannot: the firmware compiles for the device, without new warnings, and the C++ is formatted. It runs once, at the end — here.

```
pio run -e default
./bin/clang-format-fix -g && git diff --exit-code
```

`pio check` (cppcheck, as in CI) is part of the net when it is available locally.

## When the net can't run here

`pio` missing → try `pip install platformio` once. Still missing, or the toolchain download fails, or clang-format 21 is missing → the net is CI (`.github/workflows/ci.yml`: `build` for `default`, `sticky`, `x4pro`, `papermono`, plus `clang-format`, `cppcheck`, `unit-tests`). In that mode:

- report `BUILD-UNCHECKED:` with which part could not run, instead of `BUILD-GREEN:`;
- your caller pushes, then reads the CI result for the pushed commit (GitHub MCP tools: check runs or job logs) and treats a red job as a failure to bring back here.

Never claim green for a check you did not run.

## Rule #1 — never wait on the human

No one answers during this loop. A `QUESTION:` from any sub-agent goes to a fresh `guideline` agent, never to the user. The only way back to the human is the stop after 3 failed attempts (step 5).

## Rule #2 — you are the human at fix-bug's commit gate

`fix-bug` stops once, at its `git-commit` proposal, for a human go-ahead. Here, you give or refuse it, after running the checks of step 4 yourself. Never accept on the strength of an agent's report.

## Loop

### 1. Run the net
Run it. Green → report `BUILD-GREEN:` with the list of `fix:` commits made during this run, and stop.

### 2. Split the red into failures
One failure = one compiler error or new warning (file and line), one formatting diff, or one cppcheck defect. Keep the full output of each.

Take the first one in the build output: later errors are often its consequence. Handle it alone; the others wait for the next run.

### 3. Fix it

- **Formatting** → apply `./bin/clang-format-fix -g` and commit (`chore:` or folded into `HEAD` per `git-commit`). No pipeline.
- **A compile error or warning with an obvious local cause** (missing include, type mismatch, unused variable) → fix it in place, minimal, then step 4.
- **Anything that is a behaviour bug** (cppcheck finding a real defect, a warning that reveals wrong logic) → invoke the `fix-bug` skill in place. Its argument is a bug report you write, containing:
  - **Symptom**, verbatim: the compiler or cppcheck output for this failure, nothing paraphrased.
  - **Reproduction**: the command that shows it.
  - **Constraints on the fix**, to be carried into every stage:
    - The RED test is a host gtest under `test/`, registered in `test/CMakeLists.txt`, when the defect is in host-buildable code (`lib/`, pure helpers in `src/util/`). Device-only code gets no host test: say so in the commit message.
    - The fix turns the net green and keeps the host tests green.
    - Never silence a warning, never add a `cppcheck-suppress`, never skip a test to get green.
  - **Already eliminated**, on a retry (step 5): every hypothesis the previous attempts disproved, and why their commit was refused.

### 4. The commit gate — check, then accept or refuse
Check each point yourself, with commands you run:

1. The net is green for the failure you handled, and it no longer appears.
2. The host tests are green.
3. For a `fix-bug` fix in host-buildable code: the diff adds or extends a gtest that **fails on the pre-fix code** — in a throwaway `git worktree` of `HEAD` under `tmp/`, apply only the diff's `test/` changes, build and run it: it must fail. Then it passes on the working tree.
4. The diff touches only what the failure needs.

All hold → accept: commit (folded into `HEAD` when `progress.md` exists, see `git-commit`). Remove the worktree.
Any one fails → refuse, with the reason. Nothing is committed; restore the working tree to `HEAD` before the next attempt.

### 5. Count the attempt
An attempt fails when you refuse the commit, or when `fix-bug` stops on its own.

- Fewer than 3 failed attempts on this failure → back to step 3 with the same failure, extended with what was eliminated and why the last attempt failed.
- 3 failed attempts → stop. Report `BUILD-FAILED:` to the user: the failure, the three attempts, what each eliminated, and which observation is missing. This is the only moment this skill hands back to the human.

### 6. Loop
After an accepted commit, back to step 1: the net from the top.

## Output

- `BUILD-GREEN:` + the `fix:` commits made, or folded into `HEAD` (possibly none).
- `BUILD-UNCHECKED:` + which part of the net could not run here; CI is the net for it.
- `BUILD-FAILED:` + the report of step 5. The working tree is back at `HEAD`, nothing half-fixed is left behind.

Never push: the caller decides.
