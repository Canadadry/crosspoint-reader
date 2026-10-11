# Agent brief — adapt and paste

One agent, one module. Replace the `<...>`. The bold passages are the ones that
were paid for dearly: do not soften them.

---

You are reworking the comments of **<MODULE>**: `<path/module.h>` and
`<path/module.c>`. **Both together** — the trap is often stated in the header
while the check that replaces it must live in the body.

This repository has accumulated work-log narration in its comments: phase and
batch numbers, references to design documents, copied-in reference code,
justifications for how modules were split, histories of abandoned fixes. We are
removing it. **You change no behavior**: not a signature, not a value, not a
call order. The only code you are allowed to add is `#include <assert.h>` and
`assert`s, under the conditions below.

## The bar: delete by default

**A comment is deleted unless it proves it must stay.** "It is true" and "it is
useful" are not reasons: nearly every comment is both. The only question:
**with this comment deleted, would a competent reader holding only the code
reintroduce a bug that no assert and no test catches?** If not, delete it. **In
doubt, delete**: git keeps it, and I put what matters in the commit message.

- **The header ends with no comments.** Not one per declaration, not one per
  field, not a file banner. The signature and the names are the documentation.
  "Must outlive the handle", "may be NULL", "returns 0 on failure", "called
  once per frame": all deleted. Only three things may remain, listed below.
  The check `<header_residue.sh>` must print nothing on your header.
- **The body keeps only traps**, one sentence each, at the line they protect.
- **A test file keeps the reason for a literal value**, nothing else (see
  below).

Example of the expected result: `src/engine/allocator.h` went from a comment on
every declaration to none, and `allocator.c` kept two one-line traps ("`handle`
must stay first: arena_t is reached by casting the handle"; "Not
bump_zone_init: under ASan it poisons two halves, past the block").

What may survive, each in exactly one place:

1. **The meaning of a name** → `<docs/glossary.md>`, if the repository has one.
   You **do not write** to this file (other agents are working in parallel):
   you list in your report the entries to add or correct. The code does not
   repeat the glossary.
2. **A trap a future reader would reintroduce** → an
   `assert(cond && "short message")` at the exact site **if you can establish
   that it holds**, otherwise **one sentence** in the body, at that line. Read
   the next section before writing a single assert.
3. **A file format that something outside the repository must read** → it may
   stay in full in the header that writes it. A format read only by this
   repository's code or tests does not qualify: the tests pin it. Say in your
   report which header you claim this for, and why.

`TODO` is still allowed: it admits that a case is not handled.

## The assert rule — the most important one in this brief

**An assert must be verifiable, not just plausible.** You cannot run the build
or the tests (the build directory is shared), so you are tempted to reason
instead of measuring. Serious reasoning sometimes encodes an assumption rather
than an invariant.

It happened: an agent added an assert requiring a data spec to end with a comma.
The rule was true, and it was the costliest trap in the repository; the agent
had validated it by reimplementing the parser offline and running it over all 58
known specs. **The first run of the suite said otherwise**: measured, the specs
decoded from memory end with two commas, and the index does not land in the same
place. The condition was off by one, and the whole suite aborted.

So: you add an assert only if you can **establish** it, which means

- exhaustively enumerating the call sites, and showing that all of them respect
  it; or
- **measuring** (counting a length, decoding some content, running a script outside
  the target language) rather than deducing; or
- proving the invariant **locally, with no assumption at all about the callers**.

"I don't see how that could happen" is not a proof. If you cannot establish it,
you write one sentence. **One assert too many costs more than one comment too
many.**

Compute the margin before writing, when there is one (bounds, pool sizes,
counters). If the computation says no, do not add the assert — and **tell me**:
on this project, a "no" of that kind was an out-of-bounds read bug nobody had
seen.

In your report, list the asserts **considered and then rejected**, with the
reason. That list is worth as much as the ones you add.

**If your file is reached by no runnable target here** — it includes a
non-vendored dependency, no test builds it — then **you add no assert at all**,
even an established one. The net that catches wrong asserts is the test suite;
where it does not reach, a human will catch it, much later. Say so in your report
and write one sentence.

**A pre-existing assert with no message** (a bare `assert(cond)`, with next to it
the comment that would be exactly its message) is a frequent case in a
repository that has already been reworked: the first pass added the assert, the
second finds the orphaned comment. Merging them is the right transformation
**and it is a code change**: you do not make it. You keep the prose and report
it; the orchestrator merges it after your pass, in its own `refactor:` commit,
then deletes the comment.

## Verify every status claim before believing it

"Not ported yet", "never exercised", "no reader", "dead code", "this will be a
problem the day X happens": these sentences were not lies when they were
written, they aged. About fifty turned out to be false in this repository, and
several steered attention away from the one place that needed examining. Each
one gets verified (grep, caller count, reading the reference) or deleted. A
claim verified false is a result: report it.

**This also applies to strings meant for a human** — banner, help message,
status line: they are documentation that executes, and they go stale like a
comment. On this project, a rejected playback mode had disappeared from the code
and the comments but survived in the program's banner. **You check it, and a
divergence is reported — never fixed here**: it is code, and it deserves its own
`fix:` commit.

## If your module is a test file

Different rule. A test's comment carries **why this value** — which no code
comment carries. An assertion on a pixel, a range or an index cannot be
recovered without its reason: you keep those reasons, and the retention rate
will legitimately be higher. You remove work-log narration and stale claims,
nothing else.

**You do not modify or delete an existing assertion**: neither its value nor its presence. You may **add** one if you have proven it — the same burden of proof as anywhere else. List it in the report: the orchestrator will make a `test:` commit of it, separate from the `docs:` commit.

An assertion you judge **wrong, weak or misleading** — it does not prove what
its name announces — goes in your report, with what it actually proves.
It is not fixed here.

The threshold, since "a higher rate" is not one: **a sentence stays if it
explains a literal value, a choice of case or a buffer contract; it goes if it
tells where the decision came from.**

## When a comment is not quite a comment

Four cases where something that looks like a comment is read by a machine. The
usual check (identical non-comment lines) does not see them.

- **A comment line that a script prints** — its help taken from its own header
  (`sed -n '2p' "$0"`) — is a string, not a comment. Treat it as a message: the
  proof is the script's output, before and after, and the line keeps its line
  number.
- **A `#` inside a here-document** (`<<EOF`) is not a comment: it is text the
  script writes. Leave it as is, or report it.
- **An `awk` program** (or any program) **inside single quotes in a shell
  script**: a comment slipped into it can break it — an apostrophe in the text
  ("entity's") closes the string — without changing a single non-comment line.
  No comment goes in there. The check adds `sh -n` and byte-identical outputs,
  in every mode and for every refusal.
- **A file whose line numbers a tool compares** — a coverage reference, an
  `assert` message that prints its line — keeps the height of every comment
  block: a block that grows or shrinks shifts all the code after it. Check: the
  lines that are not comments stay at the same line numbers.

## Check before handing back

- No declaration and no line of code lost. Compare the preprocessor output
  (`<gcc -fpreprocessed -dD -E -P>`) before/after: it must be identical, apart
  from the `assert`s. **This check works even on a file you cannot compile**:
  the option does not resolve `#include`s, it strips comments. Never fall back
  on eyeballing.
- The number of lines of code is identical before and after. A second, free
  safety net.
- No nested block comment (a `/* */` does not nest).
- `#include <assert.h>` present if you added an assert.
- `<header_residue.sh> <your headers>` prints nothing (`-x <header>` only
  for a header you claim under destination 3).
- Count the comment lines before and after with **the command given in this
  brief**, not with your own: otherwise your figure is comparable to no other.
- Report files from the same batch that you came across without being in
  charge of them. A catch-up that leaves half of them behind is not one.

## Findings outside comments

You will find things that are not comments: a claim the code contradicts, a
latent overflow, an argument parsed differently from its usage line. Report
them in section 8, one line each, **marked `defect` only if it produces a
wrong output, a crash, an out-of-bounds access or data loss** today, on a
platform the repository builds for. Everything else is marked `note` and ends
in the commit message. You fix none of them.

## Report

Hand back your report in the format of `report-template.md`. **The "what I
hesitated to delete" section is not optional**: do not fill it out of
politeness, do not leave it out either.
