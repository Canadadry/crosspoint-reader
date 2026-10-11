---
name: tdd
description: Internal — called by prd-autopilot and fix-bug implementers. Red-green-refactor, one behavior at a time, tests through the public interface.
---

# Test-Driven Development

## Philosophy

**Core principle**: Tests should verify behavior through public interfaces, not implementation details. Code can change entirely; tests shouldn't.

**Good tests** are integration-style: they exercise real code paths through public APIs. They describe _what_ the system does, not _how_ it does it. A good test reads like a specification - "user can checkout with valid cart" tells you exactly what capability exists. These tests survive refactors because they don't care about internal structure.

**Bad tests** are coupled to implementation. They mock internal collaborators, test private methods, or verify through external means (like querying a database directly instead of using the interface). The warning sign: your test breaks when you refactor, but behavior hasn't changed. If you rename an internal function and tests fail, those tests were testing implementation, not behavior.

**Assertions complement tests.** Tests verify behavior from the outside; assertions enforce contracts from the inside. Add assertions at module boundaries (preconditions: "this argument must be a positive number") and at key invariants ("after this operation, count must not be negative"). When a test fails because of an assertion, the error points directly to the violated contract rather than to a symptom far from the cause. See [interfaces-design.md](interfaces-design.md) for where assertions belong.

See [tests.md](tests.md) for examples and [mocking.md](mocking.md) for mocking guidelines.

## Anti-Pattern: Horizontal Slices

**DO NOT write all tests first, then all implementation.** This is "horizontal slicing" - treating RED as "write all tests" and GREEN as "write all code."

This produces **crap tests**:

- Tests written in bulk test _imagined_ behavior, not _actual_ behavior
- You end up testing the _shape_ of things (data structures, function signatures) rather than user-facing behavior
- Tests become insensitive to real changes - they pass when behavior breaks, fail when behavior is fine
- You outrun your headlights, committing to test structure before understanding the implementation

**Correct approach**: Vertical slices via tracer bullets. One test → one implementation → repeat. Each test responds to what you learned from the previous cycle. Because you just wrote the code, you know exactly what behavior matters and how to verify it.

```
WRONG (horizontal):
  RED:   test1, test2, test3, test4, test5
  GREEN: impl1, impl2, impl3, impl4, impl5

RIGHT (vertical):
  RED→GREEN: test1→impl1
  RED→GREEN: test2→impl2
  RED→GREEN: test3→impl3
  ...
```

## Workflow

### 1. Planning

Nobody answers questions: decide from the PRD or bug document you were handed, and the code. Before writing any code:

- [ ] Decide the interface changes needed
- [ ] List the behaviors to test, most important first — not implementation steps. You can't test everything: critical paths and complex logic first.
- [ ] Look for [deep modules](deep-modules.md) (small interface, deep implementation)
- [ ] Design interfaces for [testability](interfaces-design.md): illegal states unrepresentable, core logic pure

A blocking question the inputs can't settle → end with `QUESTION:`; your caller gets it answered.

## Where tests live

One `tests/test_<module>.c` per module, registered in the Makefile's `UNIT_TESTS`, run by `make test`. A test drives the module's public interface through the real path — "integration-style" below means that, not a new e2e. Never add a replay to `tests/e2e/`.

### 2. Tracer Bullet

Confirm ONE behavior end-to-end, whether that means a new assertion or a deeper change:

```
RED:   Extend the integration test, or a type/signature it exercises → fails
GREEN: Write minimal code to pass → test passes
```

This is your tracer bullet - proves the path works end-to-end.

**Running tests**: `make test`, never a test binary run alone as the final check.

### 3. Incremental Loop

For each remaining behavior, extend the module's test file:

```
RED:   Extend the test, type, or signature for the next behavior → fails
GREEN: Minimal code to pass → passes
```

Rules:

- One behavior at a time
- Only enough code to pass current test
- Don't anticipate future behaviors
- Keep the test integration-level: exercise the real path end-to-end, not internals

### 4. Refactor

After all tests pass, look for [refactor candidates](refactoring.md):

- [ ] Extract duplication
- [ ] Deepen modules (move complexity behind simple interfaces)
- [ ] Apply SOLID principles where natural
- [ ] Consider what new code reveals about existing code
- [ ] Add or strengthen assertions for assumptions the code now relies on
- [ ] Run tests after each refactor step

**Never refactor while RED.** Get to GREEN first.

## Checklist Per Cycle

```
[ ] Test describes behavior, not implementation
[ ] Test uses public interface only
[ ] Test would survive internal refactor
[ ] Code is minimal for this test
[ ] No speculative features added
[ ] Assertions guard any new preconditions or invariants introduced
```
