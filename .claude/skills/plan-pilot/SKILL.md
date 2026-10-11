---
name: plan-pilot
description: Implements a whole plan file unattended, step by step — a PRD and prd-autopilot per step, or the named skill for a "skill:" step — until the plan is done.
---

# Plan Pilot

Takes one argument: the path to a plan file in `plan-write`'s format (Goal / Rules / Phases and steps / Done when / Open questions / Step notes). Runs in its own session: the plan file is all it knows of the decisions behind it.

No human responds during this run. Every `QUESTION:` from any sub-agent goes to a fresh `guideline` agent, handed the plan file path and the question; its `DECISION:` goes back to the asker.

## Start: `progress.md`

`progress.md` at the root is the run's checklist: what is done and what is left, read by the user while you work. Before the first step:

1. Run `tools/progress_check.sh --install`. From then on git refuses any commit that ticks no new `[x]` in `progress.md`; `git-commit` § `progress.md` says how each commit satisfies it.
2. `progress.md` exists and every item is `[x]` (a finished run) → `git rm` it and commit (`docs:`). Some item is `[ ]` → it is this plan's, resumed: keep it, or stop and report if it belongs to another plan.
3. No `progress.md` → write it: a nested `- [ ]` list indented by 2 spaces, one top-level item per group of steps as the plan names them (phase, part, chunk…), one child per step, `[x]` for each step that already has a note. Commit it (`docs:`).

Rules the hook enforces: an existing line keeps its text and its place; a new line never goes before an existing `[x]`; an `[x]` never goes back. A step added to the plan later goes at the end.

## Per-step cycle

Repeat until there's no next step:

### 1. Find the next step
The plan's `Step notes` section (whatever its number) is the progress record: one note per finished step, `### Step <phase>.<step>`. The next step is the first one in the plan's order without a note. Every step has one → the plan is complete: check its `Done when` list, report, and stop.

### 2. Run it

**A `skill: <name> — <scope>` step** → invoke that skill in place on the given scope, following its own unattended rules. No PRD. Each of its commits adds its own `- [x]` child (the module, the unit) under the step's item. Then run `check-e2e` and push, as prd-autopilot does after a PRD.

**Any other step:**

1. **Write its PRD.** Spawn a fresh agent running the `to-prd` skill with [prd-prompt.md](prd-prompt.md). It writes `docs/prd/triage/phase<N>-step<M>-<slug>.md`, scoped to this one step. Don't commit it on its own: it lands with the step's first chunk.
2. **Run it.** Invoke `prd-autopilot` in place, as-is. Only this step's PRD sits in `docs/prd/triage/`, so its loop stops after this step.

**Write the next PRD in parallel.** While step N is being implemented, spawn the PRD agent for step N+1 in the background — it only reads, so it never conflicts with the implementer. Its prompt adds: step N is not done yet; every assumption about N's outcome is an explicit hypothesis labelled `H-<N>-k`; read nothing uncommitted. It writes to `docs/prd/next/`, not `docs/prd/triage/`. When N's note is written, compare the hypotheses with it: all hold → move the PRD to `docs/prd/triage/`; any fails → send the same agent back to update it first. Never start N+1's implementation before N is done.

### 3. Bugs
A bug reported by any agent, at any point, that the current step did not set out to fix: invoke `fix-bug` in place on it, unattended — you are the human at its commit gate, with the checks `check-e2e` step 4 lists. It lands as its own `fix:` commit; then resume the step.

### 4. Write the step note
Append `### Step <phase>.<step>` to the `Step notes` section: what was done, what deviated from the plan and why, every scope decision `guideline` made. A few lines, facts only. Consult `plan-review` now if step 6 calls for it, so its accepted changes ride in this commit. Tick the step's item in `progress.md` (and its group's, once all the group's children are `[x]`), commit both (`docs:`) and push. This note is what the next step's PRD and any later resume read.

### 5. End of phase
Right after the last step of a phase: run `warn-review` on the code that phase touched. Feed its warnings into a `plan-review` consultation (step 6); don't act on them directly and don't drop them.

### 6. Consult plan-review, at your discretion
After any step that was risky or taught something new, spawn a fresh `plan-review` agent. Judge each proposal against "does this concretely improve the odds of success"; apply accepted ones to the plan file, in the step-note commit of step 4. No loop, no temp branch — that is plan-converge's job, before the run.

## Boundaries

- Never change a step's content or scope yourself — only accepted `plan-review` proposals change the plan, and those are method-only.
- Stop and report to the user only when the plan is complete, or when `prd-autopilot`, `check-e2e` or `fix-bug` stops with a failure.
