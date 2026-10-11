---
name: rework-comments
description: Internal — called by plan-pilot for a "skill: rework-comments" step. Cuts comments down to what the code cannot say, behind a mechanical gate.
---

# rework-comments

A repository written by successive waves of agents accumulates **work-log narration** in its comments: phase and batch numbers, references to PRDs, copied-in reference code, justifications for how modules were split, histories of abandoned fixes. It is read once, and paid for on every later read of the file.

This skill cuts that volume down to what has a reason to live in the code, **without changing any behavior**.

A worked example from start to finish (a C port of a PICO-8 cart, 18,544 → 2,966 comment lines, 47% → 12%): [worked-example.md](worked-example.md) — run under the old, looser bar; 12% turned out to still be mostly comments that should not exist. The rest of this file is the method, to apply elsewhere.

## Scope — every file, decided per file

**The scope is every source and test file**, unless the invocation names files. Whether a file needs work is decided by reading it against the bar below, **never by a ratio**, and never by a belief that a directory "was already reworked".

What happened: a catch-up pass measured 13% comments over `src/`, concluded the repository was clean, and reworked only the six densest files. `engine/` and `game/` were never opened — and `allocator.h` alone held a comment on every declaration, none of which met the bar. A low global ratio says comments are *short*, not that they *should exist*. **A ratio is a progress measure, not a scoping tool.**

Two modes remain, and they differ only in phase 1:

**Full campaign** — the repository's instructions file has no destination rule yet. Phase 1, then phase 2 over every file.

**Catch-up** — the destination rule is already in the instructions file. Skip phase 1: the glossary, if there is one, is read-only, agents propose additions in their report. Phase 2 still covers **every file in scope**; a file that already meets the bar costs one read and an empty diff, which is cheap. The destination rule lives in the repository's instructions file (`CLAUDE.md` or equivalent); the bar below is stricter, and **this skill's bar wins** for the duration of the pass.

A repository with the rule but no glossary is still a catch-up. Do not create a glossary for symmetry; create it the day a name stops carrying its meaning.

## Prerequisite, non-negotiable

**A test suite that attests the behavior, and that is green before you start.** The method adds `assert`s; a wrong assert aborts the process. On the reference project, an agent added one that brought down the whole suite (see below). The safety net caught it, not the review.

Without a test suite: add no asserts at all, and destination 2 shrinks to its prose variant. Tell the user rather than improvising (unattended: stop before phase 2 and report it).

**This is the orchestrator's obligation, not a box the agent ticks**: the agent is not allowed to run the suite (the build directory is shared). Say so explicitly in the brief, otherwise the agent believes it is working without a net — or worse, believes it has one where there is none.

**And some files are out of the suite's reach.** On the reference project, two of them included a graphics library that was not vendored, in a repository that also forbids opening a window: no runnable target builds them. A wrong assert there would survive the whole suite and only blow up in a human's hands. **In a file that no runnable target reaches, add no assert, even an established one.** List these files before opening phase 2 and tell their agents.

Record the starting ratio; it is the progress measure for every wave:

```sh
# adapt to the language; here C/C++ comments vs lines of code
cloc --by-file src/ | tail -5
```

**Give this command to the agents; it is the definition.** "Comment line" has at least three meanings (a line that is entirely comment, a line containing one, `cloc`'s own definition): without a canonical command, each agent writes its own counter and the figures are comparable neither with each other nor with yours. It is the only measure the method presents as objective; letting it depend on each agent's tooling empties it of meaning.

## The bar: delete by default

**A comment is deleted unless it proves it must stay.** The question is not "is it true?" nor "is it useful?" — nearly every comment is both. The question is: **with this comment deleted, would a competent reader holding only the code reintroduce a bug that no assert and no test catches?** If not, it goes. When in doubt, it goes: git keeps it.

By kind of file:

- **Headers carry no comments.** The signature and the names are the documentation. Two exceptions, nothing else: a `TODO`, and a file format that something outside the repository must read. A contract that matters ("NULL aborts", "returns 0 on a missing field") is either enforced by an assert in the body, or is not worth a sentence. Checked mechanically: [header_residue.sh](header_residue.sh) must print nothing.
- **Bodies (`.c`)** keep only traps, at the exact line, **one sentence** — or the assert that replaces it.
- **Tests** keep the reason for a literal value (a pixel, a range, a frame index). Everything else goes.

The reference failure for this rule is under-deletion, not over-deletion: a pass that kept every "contract" in `allocator.h` — "Handle on an already initialised zone, which must outlive it", "The block may be NULL", "Uninitialised memory; p8_fatal when the zone is full" — because each was true, short, and fitted the old destination 4. The user's verdict: none of them was useful. The calibrated result kept zero lines in the header and two one-line traps in the body.

Every comment that survives goes to **one** of these destinations:

1. **The meaning of a name** → a glossary file, one line per symbol. The code does not repeat it.
2. **A trap a future reader would reintroduce** → `assert(cond && "short message")` at the exact site if it can be checked at runtime, **otherwise one sentence in the body**, at the line it protects.
3. **A file format or a protocol that something outside the repository must be able to read** → it may stay in full in the header that writes it; it has nowhere else to live. A format read only by the repository's own code or tests is not one: the tests pin it. Pass the header to `header_residue.sh` with `-x`, and justify it in the report.

Everything else goes into the **commit message**. Git keeps it, and nobody pays for it in tokens on every read of the file. `TODO` is still allowed: it admits that a case is not handled.

## Phase 1 — full campaign only, sequential, while the prose is still there

The orchestrator does it alone, before any agent. It extracts what could no longer be reconstructed once the comments are trimmed.

1. **The glossary.** Useful when identifiers do not carry their meaning: transliterated minified code, symbols inherited from a binary format, domain abbreviations. Build it by reading every header, one line per symbol, and check mechanically that every symbol cited still exists. *Transposing*: if the names already speak for themselves, this file has no reason to exist — do not create it for symmetry. It can become a glossary of domain vocabulary, or nothing.
2. **Delete the work logs** that git now makes redundant, and patch every dangling reference.
3. **Rewire the process that produced the prose** — otherwise it grows back. Concretely: write the destination rule into the repository's instructions file (`CLAUDE.md` or equivalent), and **fix the skills that wrote narration**. On the reference project, a bug-fixing skill appended a log paragraph on every run; that step was removed, and the finding now goes into the commit message.

Commit phase 1 before opening phase 2.

## Narration does not live only in comments

A string meant for a human — banner, help message, status line — is documentation that executes, and it goes stale exactly like a comment. On the reference project, a rejected playback mode had disappeared from the comments **and** the code, but survived in the banner the program prints on every launch: the only sentence the human reads before looking at what they are about to sign off on promised a behavior that no longer exists.

Hence the rule, to put in every brief: **a string meant for a human is checked like a comment, and a divergence is reported — never fixed in this pass.** It is code: the diff would stop being purely subtractive, and the fix deserves its own `fix:` commit.

## Phase 2 — one agent per module

In a one-off catch-up, this is where you start.

One agent, one module, and **the header and the body together** (`.h` + `.c`, interface + implementation): the trap is often described in the header while the assert that replaces it must live in the body. An agent that sees only one of the two files loses half of the transformation.

The brief to copy: [agent-brief.md](agent-brief.md). The report to require: [report-template.md](report-template.md).

Group modules into coherent waves (4 to 6 agents), one wave at a time.

## The central rule: an assert must be verifiable, not just plausible

Agents cannot run the build — they share the build directory — so they **reason instead of measuring**, and serious reasoning sometimes encodes an assumption rather than an invariant.

What happened: an agent added an assert requiring a data spec to end with a comma. The rule was true, and it was the costliest trap in the repository. The agent had validated it seriously — reimplementing the parser's state machine offline and running it over all 58 known specs. The first `make test` said otherwise: measured, the specs decoded from memory end with **two** commas, and the index does not land in the same place. The condition was off by one. Assert removed, rule put back into prose.

Hence the instruction, to put in bold in every agent's prompt:

> **If it cannot establish that an assert holds other than by reasoning, it writes one sentence.** One assert too many costs more than one comment too many.

Establishing means: exhaustively enumerating the call sites, measuring a length or some content outside the target language, or proving the invariant **locally, with no assumption about the callers**. Not: "I don't see how that could happen."

Later waves refused five asserts for this reason, and one of those refusals produced the best find of the project: the agent computed the margin before writing (66 tickets in a pool for 90 possible draws), the computation said no — and that "no" was an out-of-bounds read bug nobody had seen. **Explicitly ask for the list of asserts considered and then rejected**: it is worth as much as the ones that get added.

## The other rules, all tested in the field

- **Verify every status claim before believing it.** "Not ported yet", "never exercised", "no reader", "dead code": about fifty turned out to be false. They were not lies when written, they aged — and that is what makes them dangerous; several steered attention away from the one place that needed examining. A status claim gets verified (grep, caller count) or deleted.
- **A test file has its own rule.** Its comment carries *why this value*, which no code comment carries: an assertion on a pixel, a range or a frame index cannot be recovered without its reason. The retention rate is legitimately higher there. **You do not modify or delete an existing assertion**: neither its value nor its presence. Changing what a test proves in a cleanup commit means slipping it through without review. **Adding** one is allowed if it is proven like any other assert: it is listed in the report, and the orchestrator makes a `test:` commit of it alongside the `docs:` commit.
- **Agents do not write to the glossary.** Several would share it. They *report* missing entries and errors in their report, and the orchestrator applies them in batches. On the reference project: about forty entries added this way, and **four errors by the orchestrator itself** found by its agents.
- **The "what I hesitated to delete" section is the most useful part of the report.** It is what surfaces the stale claims, the glossary errors and the bugs declared fixed that are only half fixed. Ask for it explicitly, with the instruction **neither to fill it out of politeness nor to leave it out**.

## The orchestrator's role

0. **The mechanical check works everywhere, including where compilation fails.** `gcc -fpreprocessed -dD -E -P` does not resolve `#include`s: it only strips comments. A file that does not compile here — non-vendored dependency, missing header — can therefore still be checked, byte for byte. Never tell an agent to "review the diff by hand" for those files: you would take away its only proof. A second, free safety net: the number of lines of code must be identical before and after.
1. **Compile each module in isolation** as soon as its agent hands back (`make src/obj/module.o` or the equivalent). That catches the missing `#include <assert.h>` and nested comments.
2. **Run the full suite only on a stabilized tree** — every agent of the wave has handed back. A build on a half-rewritten tree tells you nothing, and this is precisely what made it possible to attribute the assert failure correctly.
3. **Commit module by module**, with the narration in the message: the before/after ratio, the asserts added and how each was proven, the stale claims taken apart, the glossary corrections.
4. **Merge each reported bare assert with its orphaned comment** (the comment becomes the message, then goes), in a `refactor:` commit of its own, after the module's subtractive commit.
5. **Keep commits that add `abort()`s** (an assert is one) separate from those that only delete text. A purely subtractive commit must read as such, and be revertable without taking anything else with it.
6. **Recommend line-range replacement**, which refuses if the range contains anything other than comment, over exact-string replacement copied from a read. The former cannot touch code and fails loudly; the latter gets a newline wrong and pastes approximate text.
7. **Record the ratio at every wave.** It is the useful progress measure, and the only one that does not depend on judgment.
8. **The gate, per module, before its commit** — all mechanical, all must pass:
   - comment-stripped output byte-identical before/after (step 0), lines of code identical;
   - `header_residue.sh` prints nothing for the module's headers (`-x` only for an external format, named in the commit message);
   - the module compiles with the repository's warnings as errors: the byte check is blind to a `/*` inside a comment (`tools/*.sh` in prose), which `-Werror=comment` rejects;
   - the test suite green on the stabilized tree.
   Stage only the files of agents that have handed back (`git add <their files>`, never a directory): a directory sweeps in a wave still being edited.
   A failing gate sends the module back to its agent with the output; it is never waved through.
9. **Scripted deletion is allowed, only behind the gate.** A regex that strips comments is fast and right for headers, where everything goes; it is also how a trailing `/* ... */` takes the newline after it and glues two declarations together. Measured on this repository: the byte check caught it on the first try. Never script without step 0.

## Running unattended (plan-pilot)

No one answers during the run. The bar above **is** the decision: there is no calibration question to ask, and a doubt on a comment resolves to deletion. A doubt on anything else (an assert) resolves to the conservative side: no assert, one sentence.

Waves are sized as in phase 2; each module that passes the gate is committed, the next wave starts. The run stops only when every file in scope has passed the gate, or when the suite goes red and the cause is not found in the wave's own diff.

## What reaches the user

The final report is about comments: per-module ratio, the gate's results, the commits, the asserts added and rejected. **A finding outside comments is not a decision to put to the user.** Agents surface them (a claim found false, a latent overflow, an argument parsed differently from its usage line) and that is their job; the orchestrator sorts them:

- a real defect — wrong output, crash, out-of-bounds, data loss → a separate task (`spawn_task`, or one line in the report naming it as such);
- anything else → the commit message, where git keeps it.

What happened: a catch-up report ended with four "points to decide" — an argv check, a 32-bit Windows overflow on a platform nobody targets, an undocumented exit code, two assert messages. The user's answer to three of them: what is the link with your task.

## What the method does not guarantee

It produced two failures. The first: a wrong assert that aborted the whole test suite. It was written by a conscientious agent who had validated its rule offline before adding it. The review did not catch it; the first `make test` of the next wave did.

The method is therefore safe **exactly to the extent that the test suite is**. If the suite does not cover a path, a wrong assert on that path will survive until a user hits it. That is the argument for the prerequisite at the top of this file, and for the verifiable-assert rule.

The second was the opposite: a pass that changed too little. It scoped itself by ratio, skipped the directories it believed clean, and kept every true sentence as a "contract". Nothing caught it but the user reading `allocator.h`. That is the argument for the delete-by-default bar, for scoping every file, and for `header_residue.sh`: under-deletion leaves no failing test behind, so it needs its own mechanical check.
