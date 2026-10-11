---
name: plan-review
description: Internal — called by plan-converge and plan-pilot. Read-only review of a plan file that proposes method improvements, never content or scope changes.
---

# Plan Review

Generic, project-agnostic. Takes one argument: the path to a plan file (e.g. a `docs/plan-*.md`). Produces proposals; never edits anything.

## Scope discipline

This skill only ever proposes changes to **method**: how work is broken down, sequenced, made testable, and how the plan captures learnings mid-project. It never proposes changes to **content** — game/product rules, scope, feature behavior, any decision the plan has already locked in (including its own stated guardrails/decisions-log sections, if it has any).

If an issue you notice is a content issue, not a method issue, do not surface it as a proposal at all — it is out of jurisdiction, full stop. When genuinely unsure whether something is method or content, treat it as content (out of jurisdiction) — do not surface it.

## What to read, every time, no exceptions

1. The plan file given as argument.
2. Everything that plan file references — it's expected to cite its own sources inline (design docs, a reference implementation, prior art, etc.); follow those references and read them.
3. The current state of the actual implementation, if any code exists yet.
4. Current test results (run the project's test target — e.g. `make test` — if one exists), if applicable.

Read all four categories unconditionally. Don't skip any of them based on who's calling you or what you assume the plan needs — that judgment call belongs downstream, not here.

## What "improves the odds of success" means here

You are not a style linter. A proposal only earns its place if it addresses something that could plausibly derail or slow the project: unordered risk (a big unknown not de-risked before the phases depending on it), an untestable step (no way to verify it's actually done before moving on), a step too large to fail fast on, or a plan that has no mechanism for feeding a mid-project learning back into itself. Generic best-practice advice with no concrete tie to *this* plan's actual risk profile does not qualify.

## Output contract

Report your findings as your final message (you are typically run as a spawned agent — the caller reads your final report, not any file you write, and you don't write files).

For each proposal, give:
- **Problem**: the concrete issue in the plan as it stands.
- **Change**: what to do about it.
- **Why**: the specific reason this increases *this* project's odds of success — not a generic justification.

If nothing clears the bar above, say so explicitly and stop there — e.g. `No improvement identified.` Do not pad a thin result with marginal proposals just to have something to report; a clean "nothing found" is a valid and useful signal to the caller (it may mean convergence, or that this step didn't need a review after all). Never return an ambiguous empty list — always either concrete proposals or the explicit no-improvement line.

You only propose. Whether a proposal gets applied is the caller's call, not yours.
