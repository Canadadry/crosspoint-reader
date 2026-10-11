---
name: to-prd
description: Internal — called by plan-pilot for each plan step. Writes the step's PRD to docs/prd/triage/, deciding everything itself.
---

Writes one PRD from the context the caller hands over (a plan step and its plan file) and the codebase. Nobody answers questions: decide, and record each decision in the PRD.

## Process

1. Explore the repo to understand the current state of the code the step touches.

2. Sketch the modules to build or modify. Look for deep modules — a lot of functionality behind a small, testable interface that rarely changes. Decide which modules get tests; record the choice under Testing Decisions.

3. If the work is too big for one TDD cycle and a natural build order exists, add an Implementation Chunks section listing each chunk in dependency order. When in doubt, cut: a chunk too small costs a commit, a chunk too big costs a failed cycle.

4. A question you can't settle from the plan or the code → a fresh `guideline` agent, handed the plan file path and the question. Record its decision under Implementation Decisions.

5. Write the PRD to the path the caller gave (default `docs/prd/triage/<slug>.md`), using the template below, in English.

<prd-template>

```
---
title: "<feature name>"
description: "<one-sentence problem statement>"
status: needs-triage
---
```


## Problem Statement

The problem that the user is facing, from the user's perspective.

## Solution

The solution to the problem, from the user's perspective.

## User Stories

A LONG, numbered list of user stories. Each user story should be in the format of:

1. As an <actor>, I want a <feature>, so that <benefit>

<user-story-example>
1. As a mobile bank customer, I want to see balance on my accounts, so that I can make better informed decisions about my spending
</user-story-example>

This list of user stories should be extremely extensive and cover all aspects of the feature.

## Implementation Decisions

A list of implementation decisions that were made. This can include:

- The modules that will be built/modified
- The interfaces of those modules that will be modified
- Technical clarifications from the developer
- Architectural decisions
- Schema changes
- API contracts
- Specific interactions

Do NOT include specific file paths or code snippets. They may end up being outdated very quickly.

Exception: if a prototype produced a snippet that encodes a decision more precisely than prose can (state machine, reducer, schema, type shape), inline it within the relevant decision and note briefly that it came from a prototype. Trim to the decision-rich parts — not a working demo, just the important bits.

## Implementation Chunks

For big features only, when a natural build order exists among the modules: each chunk listed in dependency order, so the feature can be built incrementally.

## Testing Decisions

A list of testing decisions that were made. Include:

- A description of what makes a good test (only test external behavior, not implementation details)
- Which modules will be tested
- Prior art for the tests (i.e. similar types of tests in the codebase)

## Out of Scope

A description of the things that are out of scope for this PRD.

## Further Notes

Any further notes about the feature.

</prd-template>
