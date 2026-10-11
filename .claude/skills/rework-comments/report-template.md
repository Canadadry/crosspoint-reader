# Agent report template

Paste it at the end of the brief, or have the agent read it. The orchestrator
draws from it the commit message, the batches of glossary entries, and
the decision to rerun a module. It reads **sections 6 and 7 first**: they are
the ones that carry the results.

**An empty section is not a failed section**, and that holds for all eight. On a
catch-up of a few files, 3 and 5 are often empty by construction. Padding is
the only real failure.

---

## 1. Ratio

```
<module>.h   <N> → <n> comment lines   (<X> lines of code)
<module>.c   <N> → <n>                 (<X>)

check: preprocessor diff before/after <identical | difference, which one>
       lines of code <identical | difference>
       header_residue.sh <empty | -x <header> claimed as an external format>
```

The check goes here, next to the ratio: it is the first thing the orchestrator
must read, and the only proof that the cleanup broke nothing.

## 2. What I kept, and under which destination

One line per comment kept, with its destination (2 = trap,
3 = external format). A header should appear here only for a `TODO` or an
external format. Every line must answer: what bug would a reader
reintroduce without it, and why no assert or test catches it.

- `other()` (body) — trap: <the bug it prevents>; not an assert because <…>.

## 3. Asserts added

Empty on a test file, and empty on a file that no runnable target reaches: in
both cases the pass adds none.

One block per assert. **The proof method is mandatory**: enumeration of the call
sites, measurement, or local proof with no assumption about the callers. "I
don't see how that could happen" is not a proof method.

```
site      : <file:line>
condition : assert(<cond> && "<message>")
replaces  : <the comment that stated the trap>
proof     : <enumeration / measurement / local> — <the detail that establishes it>
```

## 4. Asserts considered and then rejected

As important as the previous section. One block per candidate:

```
condition considered : <cond>
why rejected         : <what the examination showed>
replaced by          : <the one sentence, or nothing>
```

If the margin computation said no, give the numbers. It has already revealed a
bug.

## 5. Glossary entries to add or correct

You do not write to the glossary. Propose; the orchestrator applies in batches.

- add: `<symbol>` — <meaning, one line>
- correct: `<symbol>` — the glossary says "<X>", the code says "<Y>", verified
  by <…>

## 6. Status claims verified false

One line each: what the comment claimed, what the verification showed, how you
verified it.

## 7. What I hesitated to delete

**Mandatory. Do not fill it out of politeness, do not leave it out.**

What made you hesitate and you deleted anyway, or kept without being sure: a
long justification that cannot be reconstructed from anywhere else, knowledge
that risks no longer being written down anywhere after your pass, a comment that
contradicts the code, a bug announced as fixed of which you find only half the
fix. Say what is lost and what it would cost to reconstruct.

This is the section that, on the reference project, surfaced the stale claims, the
glossary errors and a bug declared fixed that was only half fixed.

## 8. What stays as is, and why

What you could not rework without stepping outside your mandate (touching an
assertion, modifying code), and what would need to be
decided.

Findings outside comments go here too, one line each, marked `defect` (wrong
output, crash, out-of-bounds, data loss, today) or `note` (everything else).
The orchestrator turns a `defect` into a separate task and a `note` into a line
of the commit message; neither becomes a question to the user.
