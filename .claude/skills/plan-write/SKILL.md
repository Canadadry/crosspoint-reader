---
name: plan-write
description: Writes the plan file from the current grilling conversation, ready for plan-converge then plan-pilot in new sessions.
---

# plan-write

Turns what the current conversation decided into `docs/plan-<N>.md`, N being one more than the highest existing plan number (check `git log --all` too: finished plans are deleted).

## The rule: the file stands alone

plan-converge and plan-pilot run in **new sessions**: they never see this conversation. Everything decided here goes into the file, or it is lost — every decision, every option rejected and why, every guardrail, every "not this time". Write for a reader who has only the file and the repository.

Before writing, list the decisions of the conversation and check each one lands in a section below. Ask the user only about a decision the conversation left ambiguous.

## Format

```
# Plan N — <title>

## 1. Goal
The outcome, and the goals that justify every rule below.

## 2. Rules
Guardrails, locked decisions, options rejected and why, out of scope.

## 3. Phases and steps
### Phase 1 — <name>
1. <step: one verifiable outcome, and how it is verified>
2. skill: <skill-name> — <scope>   (a step run by calling that skill directly)

## 4. Done when
The checks that close the plan.

## 5. Open questions
What the conversation left unresolved. Empty if nothing.

## 6. Step notes
(empty — plan-pilot appends a note after each step)
```

- Every step is small enough to fail fast, and says how it is verified.
- A step that is entirely a skill's job (`rework-comments` over a directory, …) is written `skill: <name> — <scope>`: plan-pilot calls that skill instead of writing a PRD.
- English, per `CLAUDE.md`. No "Draft:" line.

## End

Commit the plan alone, `docs: add plan N — <title>`: a new session only sees what is committed. Then print the next command, to run in a new session:

```
next: /plan-converge docs/plan-N.md
```
