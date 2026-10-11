---
name: check-e2e
description: Internal — called by prd-autopilot after each PRD, and by plan-pilot after a "skill:" step. Runs make e2e and drives every red replay to green through fix-bug, one failure at a time.
---

# check-e2e

`make test` is the dev loop: unit tests and static checks, seconds. `make e2e` is the net: every e2e of `tests/e2e/`, replayed from the title screen, captured and compared to its `.json.sha256` reference, about a second. It runs once, at the end — here.

A red e2e is always a bug. An intended rendering change is out of scope for this skill: never run `make e2e-baseline`, never edit a `.sha256` reference to get green.

Every e2e bug leaves behind a unit test that catches it in `make test`. That is the point: the next time, the dev loop catches it in seconds instead of the net catching it later.

## Rule #1 — never wait on the human

No one answers during this loop. A `QUESTION:` from any sub-agent goes to a fresh `guideline` agent, never to the user. The only way back to the human is the stop after 3 failed attempts (step 5).

## Rule #2 — you are the human at fix-bug's commit gate

`fix-bug` stops once, at its `git-commit` proposal, for a human go-ahead. Here, you give or refuse it, after running the checks of step 4 yourself. Never accept on the strength of an agent's report.

## Loop

### 1. Run the net
Run `make e2e`. Green → report `E2E-GREEN:` with the list of `fix:` commits made during this run, and stop.

### 2. Split the red into failures
One failure = one `frames: DIVERGENT <events>` line. Keep the full output of each.

Take the failure whose replay has the fewest frames (the size of `tmp/frames/<events>.bin` divided by 9600): it is the cheapest to diagnose. Handle it alone; the others wait for the next `make e2e`, since a fix often repairs more than one.

### 3. Invoke fix-bug on it
Invoke the `fix-bug` skill in place. Its argument is a bug report you write, containing:

- **Symptom**, verbatim: the `make e2e` output for this failure, nothing paraphrased.
- **Reproduction**: the replay command — `./tmp/replay <events> <capture> <dump>` — and `make e2e-ref` for the first divergent frame against the commit that set the reference. The `.txt` next to each e2e says what it does and at which frames.
- **Constraints on the fix**, to be carried into every stage:
  - The RED test is a **unit test run by `make test`**, registered in the Makefile's `UNIT_TESTS`. Never a new e2e in `tests/e2e/`.
  - Its cost is **negligible**: it builds the minimal state that reaches the cause, it does not replay a recorded session, and it runs well under 50 ms. Needing a long event sequence to go red is the sign the real cause has not been found — go back to diagnosis, don't ship it.
  - The fix turns **both** the new test and `make e2e` green.
  - A divergence is a bug. Never re-baseline, never edit a `.sha256`.
- **Already eliminated**, on a retry (step 5): every hypothesis the previous attempts disproved, and why their commit was refused.

### 4. The commit gate — check, then accept or refuse
When `fix-bug` proposes its commit, check each point yourself, with commands you run:

1. The diff adds (or extends) a test under `tests/`, and its binary is in `UNIT_TESTS`.
2. That test does not replay any file of `tests/e2e/` and runs under 50 ms (time `./tmp/<test>`).
3. It **fails on the pre-fix code**: in a throwaway `git worktree` of `HEAD` under `tmp/`, apply only the diff's `tests/` and `Makefile` changes, build and run the test — it must fail. Then it passes on the working tree.
4. `make test` and `make e2e` are green, and the failure you handed over no longer appears.

All four hold → accept: `fix-bug` commits (folded into `HEAD` when `progress.md` exists, see `git-commit`). Remove the worktree.
Any one fails → refuse, with the reason. Nothing is committed; restore the working tree to `HEAD` before the next attempt.

### 5. Count the attempt
An attempt fails when you refuse the commit, or when `fix-bug` stops on its own (cause unproven after two debug agents, verification failing twice).

- Fewer than 3 failed attempts on this failure → back to step 3 with the same failure, the bug report extended with what was eliminated and why the last attempt failed.
- 3 failed attempts → stop. Report `E2E-FAILED:` to the user: the failure, the three attempts, what each eliminated, and which observation is missing. This is the only moment this skill hands back to the human.

### 6. Loop
After an accepted commit, back to step 1: `make e2e` from the top.

## Output

- `E2E-GREEN:` + the `fix:` commits made, or folded into `HEAD` (possibly none).
- `E2E-FAILED:` + the report of step 5. The working tree is back at `HEAD`, nothing half-fixed is left behind.

Never push: the caller decides.
