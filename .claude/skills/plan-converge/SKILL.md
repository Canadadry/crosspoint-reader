---
name: plan-converge
description: Sharpens a plan file before plan-pilot: loops plan-review rounds, applies what is worth it, stops when two rounds come back empty.
---

# Plan Converge

Generic, project-agnostic. Takes one argument: the path to a plan file. Runs a batch pre-flight pass — not meant to run mid-implementation (that's `plan-pilot`'s job, which consults `plan-review` directly and more lightly).

## Loop

Repeat each round:

1. Spawn a fresh `plan-review` agent against the plan file's current state. Pass it the level rule from step 3, so it doesn't look below the plan's level.
2. Read its report: a list of proposals, or an explicit no-improvement line.
3. Judge every proposal yourself, one at a time, against two questions, in this order:
   - **Level** — *is this at the plan's level of detail?* The yardstick is the plan as it stood before round 1, not as the previous rounds left it. Reject anything more detailed than that yardstick, even when it's true. Otherwise every round finds something finer, the plan drifts down one level at a time, and the loop never runs empty.
   - **Value** — *does this concretely improve the project's odds of success?* Reject anything that's merely plausible, stylistic, or not clearly worth it.

   A proposal that fails either question is rejected, even if `plan-review` let it through.
4. Apply every accepted proposal to the plan file directly (edit it). If a round accepts nothing, apply nothing and don't commit — an empty round still counts for the stop condition below.
5. If anything was applied, commit it on the temp branch (see Git mechanics).

## Stop condition

Stop the loop as soon as either is true:

- **Two consecutive empty rounds** — round N applies nothing and round N+1 also applies nothing. Converged.
- **Oscillation** — a round's accepted proposal would undo or reverse what the immediately preceding round just applied. Stop right away, don't wait for a repeat.

No other cap is needed — these two conditions bound the loop on their own.

## Git mechanics

This is where traceability lives — never as prose injected into the plan file itself, which must stay clean.

1. Before round 1, create a temporary branch off whatever branch is currently checked out (record that original branch's name).
2. Each round that applies something gets its own commit on the temp branch. Commit message: what was applied and the justification `plan-review` gave for it.
3. On stop, squash all commits made on the temp branch into a single commit, land it on the original branch, and delete the temp branch. The user ends up back on the branch they started on, with one clean commit.
4. Local-only: no push, no force-push, nothing touching a remote.
5. Standard safety: `git status` before anything destructive; never skip hooks; these branch/squash operations are safe here because they're local and never rewrite the original branch's existing history — only append to it.

## Boundaries

- Never touch content/scope — that filtering already happened inside `plan-review`; if a proposal that reaches you looks like it crossed that line anyway, reject it rather than second-guessing `plan-review`'s job.
- Don't reuse this skill's loop/git machinery from other contexts — `plan-pilot` calls `plan-review` directly during active development instead, without a loop or a temp branch, because commits are already landing for real work by then.

## End

Report the rounds (applied, rejected, why it stopped), then print the next command, to run in a new session:

```
next: /plan-pilot <plan file>
```
