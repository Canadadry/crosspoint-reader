# PRD-writing agent prompt

Fill in the placeholders and hand this as the entire prompt to a fresh agent running the `to-prd` skill.

```
Write a PRD for the next step of the plan, following the `to-prd` skill.

Plan file: <path to plan file>
Phase: <phase number and name>
Step: <step number> — <step text, copied verbatim from the plan>

Read the plan file yourself: its Goal, Rules and Step notes are the
context. The step notes record what earlier steps actually did.

Scope: this PRD covers this one step, no more, no less. If the step is
too big for one TDD cycle, split it with to-prd's Implementation Chunks
— never fold other plan steps into it.

Output: docs/prd/triage/phase<N>-step<M>-<slug>.md — nothing else.
```
