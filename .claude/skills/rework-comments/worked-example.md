# The worked example — a C port of a PICO-8 cart

The project this skill was drawn from. Read it as an example, not as the
definition: the file names, the notion of a glossary and the reference
"the cart" are specific to this repository.

## The starting point

**18,544 comment lines for 39,274 lines of code — 47%.** Almost all of it was
work-log narration: phase and "chunk" numbers, references to PRDs, the cart's
Lua code copied above every ported function, justifications for how modules
were split, histories of abandoned fixes. Result: **2,966 lines, 12%**, with no
behavior change and the test suite green at every step.

The Lua code copied into comments had a reason at the time: it was the
verification method of the porting phase. It was replaced by the recorded replay
(9,729 frames compared) — the comment no longer verified anything, it was pure cost.

## The three artifacts produced

- `CLAUDE.md`, "Comments" section: the destination rule — four at the time,
  two today (a trap, a C contract) —, in the instructions file
  every session rereads.
- A glossary of names: 191 symbols, one line each. The cart is minified into
  two-letter names and the port transliterated them literally: the **name** was
  therefore already in the code, it was the **meaning** that was missing, and it
  could be deduced neither from the C nor from the Lua. A rename to full English
  names has since replaced it: the name carries the meaning, and the "meaning of
  a name" destination has been dropped.
- A file of numbered divergences from the cart, re-checked against the code one
  by one, which the code cited through bare markers. Once the C became the
  reference, each entry was fixed, turned into a trap comment or dropped, and
  the file deleted.

## Phase 1

Built by reading the 13,195 header lines while the prose was still there. Three
findings came up at that point, none of which would have survived phase 2:

- a stale claim ("this table is empty compared to the real `map.json`") that the
  plan had recorded as resolved long ago;
- **A recorded divergence had become reachable without anyone noting it**: the comment said
  "this will be a real gap the day `spr` is registered as a constructor", and
  initialization does register it;
- **A recorded divergence contradicted a decision** of the plan ("no native floating
  point") while `p8_fixed_cos/_sin` go through the host's `cos()`.

Rewiring the process, in the same commit: the rule in `CLAUDE.md`, and the "Log
the finding" step removed from the `fix-bug` skill, which wrote a log paragraph
into the repository on every bug fix — and required a second `docs:` commit for
it. Without this step, the prose grows back.

## Phase 2 — what the waves produced

About twenty modules, in waves of 4 to 6 agents. A few results that illustrate
each rule:

- **The assert that broke the suite** (the spec parser, a module since removed
  along with the Lua layer): required a trailing comma; the rule was true,
  validated offline on 58 specs, off by one in reality — specs decoded from
  memory end with **two** commas. Removed, and the rule put back into prose with
  what the measurement established. No attempt to patch the condition: a second
  badly stated invariant costs another round, and the rule allows prose.
- **The refusal that found a bug** (weighted planet draw): the agent wanted
  `assert(state->tickets_count > 0)`, computed the margin before writing — 66
  tickets for 90 possible planets — and gave up. On checking, it was worse than
  its conclusion: with an empty pool, the index falls to 0, and the code reads
  `items[0]` on a list whose array can be NULL. Recorded at the time as a divergence, since fixed: the pool now refills.
- **The asserts rejected by the plumbing**: `i >= 1` in `p8_sub` while
  `unstash()` calls it with 0 at the end of every string; `frames != NULL` in
  `p8_input_save` while a test passes NULL on purpose. Both would have broken
  the suite. The "considered and then rejected" section surfaced them.
- **The only assert established by measurement** in its wave: the length of the
  `unstash` alphabet, counted outside the C — 51 characters, newline included.
  Getting it wrong by one silently shifts the whole decoding.
- **The test files**: 1,346 → 591 lines, the highest retention rate of the
  project, and that is intended. Three files left untouched. Two claims that
  contradicted themselves: a test announcing that it "asserts nothing" while it
  asserts six times.
- **The orchestrator's errors found by its agents**: four, including a glossary
  entry that attributed a flag to the difficulty screen when its only writer is
  the pause menu, and a header announcing 8 call sites with their line numbers
  — there are 5.
- **A loss accepted and reported**: the derivation of a 5,000-credit threshold
  (34 lines tracing back through four modules) cut down to its conclusion,
  because it can be reconstructed from named sources. The 20,000 cap keeps its
  full justification — the 16.16 format caps out around 32,768 — because that
  one cannot be reconstructed from anywhere.

## The ratios, wave by wave

They do not look alike, and that is normal: the plumbing carried little
narration (183 → 105), the gameplay modules carried a lot (722 → 133 for
`collect` + the spec parser, 1,021 → 124 for the three research modules), the
tests keep their "why this value" (1,346 → 591). A low ratio is not better
work: it is the nature of the file.
