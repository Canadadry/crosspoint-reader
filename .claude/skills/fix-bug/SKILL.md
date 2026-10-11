---
name: fix-bug
description: Fixes one reported bug end to end — proven cause, failing test, minimal fix, independent check, commit. Also called unattended by check-build and plan-pilot.
---

# fix-bug

Drives one reported bug to a committed fix through four fresh, memoryless agents. Each stage hands the next one **evidence**, never an opinion: the debug agent hands over a proof, the implementer hands over a failing-then-passing test, the verifier hands over its own independent reproduction.

**Argument**: the bug report — free text, or a path to a file holding it.

One invocation = one bug. It is not a loop over a backlog; to chain bugs, invoke it again.

## Two modes

**Called by the user.** No report given → ask for the symptom and how they hit it. Then the run is unattended until stage 4, where `git-commit`'s proposal waits for the user's go-ahead. Those are the only two moments this skill talks to a human.

**Called by `check-build` or `plan-pilot`.** Nobody is there. The caller writes the report, and the caller is the human at the commit gate: it checks the fix itself and accepts or refuses.

## Rule #1 — the sub-agents never wait on a human

Any `QUESTION:` from a sub-agent is answered by a fresh `guideline` agent, relayed back — never by pausing for the user.

This bites hardest on stage 1: tell the debug agent that it runs every test itself, on the host, so it designs tests it can run rather than tests that need the reporter's device.

Be honest about what this costs: the bug was very likely observed on the e-reader (ESP32-C3, real SD card, real Wi-Fi, e-ink display), while this pipeline proves causes on a Linux host with no hardware. A cause proven here is proven *about the code*, not about the device. When the debug agent can only reach a device-dependent hypothesis (heap pressure, SD timing, TLS, display refresh), that is an `UNPROVEN:` (see stage 1's gate) — not a green light; the missing observation is usually a serial log from the owner (`python3 scripts/debugging_monitor.py`).

## Rule #2 — no fix before a proven cause

Stage 2 never starts on a plausible cause. The gate after stage 1 is the whole point of this pipeline; waved through, this is just "guess a fix" with extra agents.

## Constraints every sub-agent inherits

Put these in each sub-agent prompt — a fresh agent knows none of them:

- **No hardware.** There is no device, no flashing, no serial port. Reproduction runs on the host: a gtest under `test/`, or a throwaway host program in `tmp/` built against the host-buildable code (`lib/`, pure helpers in `src/util/`). Device-only code (HAL, display, Wi-Fi, activities) is reasoned about from the source and `CLAUDE.md`, never assumed to behave like the host.
- **Only the orchestrator touches git.** Sub-agents never commit, stage, stash, branch or push.
- **The suite is the host tests**: `cmake -S test -B build/test -G Ninja && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`. The firmware build is `pio run -e default` (`check-build`).
- **The project rules are `CLAUDE.md`**: 380 KB RAM ceiling, HAL-only SD access, `makeUniqueNoThrow` instead of bare `new`, `LOG_ERR` + `return false` error handling, `tr()` for UI strings. A fix that breaks one is not a fix.
- Throwaway repro programs go in `tmp/`, never in `src/`, `lib/` or `test/`.

## Pipeline

### 1. Prove the cause — fresh `help-me-debug` agent

Spawn a fresh, memoryless agent running the `help-me-debug` skill. Hand it the bug report **verbatim** — never your paraphrase, and never your own early theory. A summary is exactly where an unproven hypothesis quietly becomes a premise.

Its mandate: understand and prove, **change nothing** under `src/`, `lib/` or `test/`. It may build, run, instrument throwaway copies in `tmp/`, and read anything.

Reporting contract:
- `PROVEN:` followed by the bug document from `help-me-debug`'s own template (Summary / Reproduction / Established cause / Scope / Inputs for prioritization / Unverified leads).
- `UNPROVEN:` followed by what was eliminated, what remains open, and exactly which observation is missing.
- `QUESTION:` for anything blocking, resolved via a `guideline` agent (Rule #1).

**Gate — read the proof, don't skim it.** Every claim about the cause must carry a file excerpt with path and line numbers, the output of a command actually run, or sourced documentation with proof of applicability. "Consistent with", "would explain" and "most likely" are not proofs.

- Proof holds → stage 2.
- `UNPROVEN:`, or a "proof" that is really a chain of reasoning → spawn **one** more debug agent, handed everything already eliminated so it doesn't retrace it. Still unproven after that → stop and report back what is known and which observation is missing. Stopping here is a legitimate outcome of this skill, not a failure of it.

### 2. Fix it — fresh `tdd` agent

Spawn a fresh, memoryless agent running the `tdd` skill. Hand it the stage-1 bug document **verbatim** as its context, plus the constraints above.

Its mandate, in this order:

1. **RED first**: a gtest that fails *because of this bug*, exercising the real path through the public interface — never one that asserts the shape of the fix. Reproduce through the same entry point the device code calls, never by calling an inner helper directly: bugs hide in the gap between "a test calls it" and "the device reaches it". When the bug lives in device-only code that no host test can build, say so in `DONE:` and keep the fix minimal; the commit message records that no host test covers it.
2. **GREEN**: the minimal fix the proven cause justifies. Nothing adjacent, no cleanup, no refactor of neighbouring code.
3. The host tests fully green.

It updates `USER_GUIDE.md` only if user-visible behaviour changed. It never touches git — stage 4 owns that.

Reporting contract: `DONE:` plus what the test asserts, which files and lines changed, and why that is the minimal fix — or `QUESTION:` (Rule #1). `DONE:` means implemented, **not** verified.

### 3. Verify independently — fresh agent, no skill

Spawn a third fresh, memoryless agent. It is not handed the implementer's reasoning as truth; it re-establishes the facts itself:

1. **Prove the new test actually detects the bug**: rebuild a throwaway copy of the pre-fix code under `tmp/` (or revert the fix in a scratch copy) and show the new test failing against it, then passing against the tree. A test that passes both ways proves nothing — this repo already caught two such tests exactly this way.
2. Run the host tests in full.
3. Run the sanitizer build if the bug was a memory bug (use-after-free, null deref, overflow): `cmake -S test -B build/test-asan -G Ninja -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" && cmake --build build/test-asan && ASAN_OPTIONS=alloc_dealloc_mismatch=0 ctest --test-dir build/test-asan --output-on-failure`, 0 ASan/UBSan errors. The `ASAN_OPTIONS` is needed because `test/font_cache_manager/` replaces the global `operator new` with `malloc` to count allocations, which ASan otherwise reports as a mismatch.
4. Read the diff: it must touch only what the proven cause justifies, and nothing else.

Reporting contract: `VERIFIED:` plus the evidence for each point, or `FAILED:` plus what broke.

On `FAILED:`, route by kind. A mechanical failure (test doesn't compile, wrong path, suite breakage) goes back to stage 2 with the detail. A fix that doesn't actually remove the symptom means the cause wasn't the cause: back to **stage 1**, not stage 2, handed the verifier's evidence. After one such round trip, stop and report back rather than spinning.

### 4. Commit the fix

Invoke the `git-commit` skill **yourself, in place** — never in a sub-agent. Its proposal goes to whoever holds the gate (see Two modes): the user, or your caller.

The commit message is where the finding is recorded — no journal file, no prose in the code. Write it in English, strictly factual, from the stage-1 bug document and the stage-3 evidence:

- the symptom as reported, and what surfaced it (owner on the device, serial log, host test, sanitizer, CI);
- **the hypothesis that was disproved**, when there was one — as valuable as the cause itself, since it's what spares the next reader the same detour;
- the proven cause, with file/function references;
- the fix retained, and what was deliberately *not* changed.

Write it yourself rather than delegating: you hold both the proof and the verification, a fresh agent would hold neither.

What the commit message must NOT absorb, because it belongs in the working tree instead:

- a trap a future reader could reintroduce → a host test that fails if it comes back, or a short comment at the exact spot (`CLAUDE.md` § Comment Style). On the device, a broken invariant is handled with `LOG_ERR` + `return false`; `assert` only for an impossible state (`CLAUDE.md` § Error Handling Philosophy).

Scope the commit to the code fix and its test (`fix:`). With `progress.md` at the root, `git-commit` folds it into `HEAD` instead (§ `progress.md`): the message above goes into `HEAD`'s body.

## Stopping conditions

Stop and report back to your caller when the cause can't be proven after two debug agents, when a `FAILED:` verification survives one round trip, or when the commit proposal is refused. Never widen the fix, never skip or weaken a test to get green, and never commit on an unproven cause.
