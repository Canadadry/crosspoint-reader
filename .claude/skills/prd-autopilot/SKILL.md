---
name: prd-autopilot
description: Internal — called by plan-pilot for each step. Implements every PRD in docs/prd/triage/ via tdd, commits, runs check-build, pushes; repeats until the queue is empty.
---

# PRD Autopilot

Runs the PRD triage queue unattended: implement, verify, commit, check the build net, push, repeat.

## Rule — never wait on the human
Never relay a `QUESTION:` to the user; resolve it via a fresh `guideline` agent and send its `DECISION:` back to the asker.

## Per-cycle workflow

### 1. Pick the next PRD
Take the lowest-numbered file in `docs/prd/triage/*.md`; if none remain, report the PRDs completed this run and stop.

If the PRD has an `## Implementation Chunks` section, treat each chunk as its own unit of work: run steps 2-4 once per chunk, in dependency order, each with its own implementer, verifier and commit. Otherwise the whole PRD is one unit.

### 2. Spawn a fresh implementer agent
A new, memoryless agent (no isolation) implements the PRD — or only the current chunk — via the `tdd` skill, updates `USER_GUIDE.md` if user-visible behavior changed, and never touches git.

Reporting contract: `QUESTION:` + question when blocked, or `DONE:` + summary — `DONE:` means implemented, not verified.

### 3. Verify
A second fresh agent reruns the host tests (the fast loop — never `pio run` here):

```
cmake -S test -B build/test -G Ninja && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j
```

and confirms the diff touches the PRD's expected files.

It reports `VERIFIED:`, or `FAILED:` plus detail — which stops the loop and goes to the user, nothing committed.

### 4. Commit
On `VERIFIED:`, run the `git-commit` skill in place for this chunk (or the whole PRD). You are the gate: accept when the verifier's checks hold.

With `progress.md` at the root: the PRD's first commit adds, under the current step's item, one `- [ ]` child per chunk (one for a PRD without chunks), and stages the PRD file itself; every chunk's commit ticks its child.

More chunks remain → back to step 2 for the next one; the PRD stays in triage until its last chunk lands.

### 5. Check the build net, push, graduate
Once the last (or only) chunk is committed, invoke the `check-build` skill in place.

- `BUILD-GREEN:` → `git rm` the PRD from triage and commit that deletion (`docs:`, naming the file; folded into `HEAD` when `progress.md` exists, see `git-commit`), then `git push -u origin <current branch>`. This is the only push of the cycle: the remote only ever receives states where the net is green.
- `BUILD-UNCHECKED:` → the net is CI. Push as above, then read CI on the pushed commit (`check-build` § When the net can't run here). Green → graduate as for `BUILD-GREEN:`. Red → bring the failing job back to `check-build` as its failure, then push the fix.
- `BUILD-FAILED:` → stop the loop and report to the user. Don't push, and leave the PRD in triage.

### 6. Loop
Back to step 1 until the queue is empty. Continue in place — never re-invoke `prd-autopilot` through the Skill tool, which restarts the loop and drops this run's progress.

## Stopping conditions
The queue empties, the user interrupts, a verifier reports `FAILED:`, or `check-build` reports `BUILD-FAILED:`.
