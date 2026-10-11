---
name: help-me-debug
description: Internal — called by fix-bug. Proves the cause of one bug with evidence; changes nothing, fixes nothing.
---

# help-me-debug — understand a bug, with evidence

A bug understood is a bug half-solved. The goal of this skill is NOT to produce the most likely solution, nor to fix anything. It is to establish **the real cause** of a bug, step by step, without ever jumping to conclusions.

The main trap in debugging isn't a lack of ideas: it's overconfidence. The problem *looks* obvious, the hypothesis *seems* solid, and a fix gets proposed based on a cause that was assumed but never verified. This skill exists to resist that slope. Here, only facts count.

## Core rule: no claim without proof

Every claim about the cause of the bug must come with its proof. Valid proof is one of these:

- **A file excerpt** — with its path and line numbers, actually read (not reconstructed from memory).
- **The output of a command actually executed** — not the imagined result of a command one pictures running.
- **A sourced documentation excerpt** — never quoted from memory — **together with proof of applicability**: that the package is the one actually in use, that the version matches, and that the relevant code is in fact what executes in the bug. Correct docs that describe a different version or a different usage prove nothing.

If a claim cannot be tied to one of these forms of proof, it remains an **unproven hypothesis** and must be named as such.

## The investigation loop

Follow this loop, returning to it as many times as needed:

1. **Establish the symptom as a raw fact.** Exact error message, reproduction conditions, what works vs what doesn't. At this stage, no interpretation: describe what is observed, not what is inferred from it.

2. **List hypotheses without believing them.** Enumerate the possible causes, marking each "unproven." Plausible is not true. Resist the urge to favor a hypothesis because it "stands to reason."

3. **Define the discriminating test.** For each hypothesis, ask: which observation would confirm it, and above all which would eliminate it? Prefer the cheapest test that separates the most hypotheses. A good test eliminates; it doesn't just reinforce.

4. **Gather the proof.** Run the test and tie the result to one of the three forms of proof above. You run every test yourself, headless (see below).

5. **Update, then start over.** Depending on the result, eliminate or confirm. Until the cause is **established by proof** (not merely made probable), return to step 2 or 3. Don't stop on a hypothesis simply because it's well-ranked.

## Who runs the test

You do, headless: nobody else is there. No window, no X server (`CLAUDE.md`): reproduce through a scripted `events.json`, a fixed frame count and a text dump. Throwaway programs and instrumented copies go in `tmp/`; never change `src/` or `tests/`.

Your environment likely differs from the reporter's (a real window, a real OS). A test that only their environment could run is not a proof you can get: name it as the missing observation, never presume its result.

## When the report jumps to a conclusion

The bug report, or your caller, may name a cause ("it's the cache"). It may be right: the reporter knows things you don't. But it is a hypothesis like any other — list it, test it, and give it no head start.

## Explicit prohibitions

Never do the following — it's exactly the slope this skill fights:

- Say "it's surely X" and treat it as settled without proof.
- Quote documentation from memory, or rely on it without having verified it applies to the real version and usage.
- Assume a piece of code runs, or in what order, without confirming it.
- Assume a version, an environment variable, or a configuration.
- **Propose a fix before the cause is proven.** This is the central mistake. As long as the cause isn't established, any fix is a disguised guess.

## Stopping point: the bug document

This skill's work stops the moment the cause is **proven**. Fixing the bug is not its purpose.

Report `PROVEN:` followed by the bug document below, or `UNPROVEN:` followed by what was eliminated, what remains open, and exactly which observation is missing.

```markdown
# [Short, factual bug title]

## Summary
One to three sentences: what doesn't work, observed, not interpreted.

## Reproduction
Precise steps to reproduce, with the required environment
(versions, variables, data). Someone else should be able to
reproduce it following this section alone.

## Established cause
The real cause, as proven during the investigation.
Each claim accompanied by its proof: file excerpt
(path + lines), command output, or sourced docs + applicability.

## Scope
What is affected and what is not. Conditions of occurrence.

## Inputs for prioritization
Observed frequency, severity, any known workaround.
(Facts, not a verdict — the team prioritizes.)

## Unverified leads
Hypotheses ruled out or untested, to spare the team
from retracing the same path. Clearly marked "unproven."
```

Keep the document strictly factual: it serves as documentation and communication, not as a case for a solution.
