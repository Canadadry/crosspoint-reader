---
name: warn-review
description: Internal — called by plan-pilot at each phase end. Flags design smells and CLAUDE.md rule violations as bare warnings, never fixes.
---

# Warn Review

Flags design smells as warnings only. Never suggests fixes, causes, or direction - just states what's wrong and where. The caller (plan-pilot, through plan-review) decides what to do about them.

## Scope

- Given paths (plan-pilot passes the code a phase touched): review only those.
- Given no argument: recursively scan the current working directory. Respect `.gitignore`. Skip build output, vendor/third-party code, binary assets, and `.git`. Restrict to the project's main detected language's source files.

## What to flag

Design smells only - never correctness bugs, never simplification/efficiency nitpicks. This is a smell, not a science: judge holistically, don't apply numeric thresholds or ratios.

Checklist (each is an outside-in hint that something is off):

1. No lib/utils/shared package despite repeated logic across files - whoever built this assumed nothing here would ever be reused.
2. A comment block longer than the code it explains, on a path that isn't inherently complex - if it needs a paragraph to explain, is the code itself too hard to follow?
3. A generic name (History, Manager, Handler, Utils, Data...) whose actual behavior doesn't match that concept - read the file and judge the mismatch yourself, don't just pattern-match the name.
4. A file dramatically larger than everything else, especially an entry point - what is this hiding that should've been split out?
5. A module required/imported by nearly everything else - a hidden hub nobody can avoid depending on.
6. Config values or magic numbers scattered inline with no central place - nobody expected this to need tuning or reuse.
7. Dependency direction should form a tree: main depends on specific modules, specific modules depend on generic ones from lib - never the reverse. Tangled or flat imports with no such layering is a smell.
8. The internal design should be readable from folder/file names alone. If you can't infer the architecture just by browsing the tree, that's the smell.
9. The largest or most complex file has little to no direct test coverage, relying only on broader end-to-end checks - the code most likely to hide bugs is the code least protected against them.
10. A systematic audit/sweep's completeness claim (e.g. "N sites, all handled") with no mechanism (test, CI check, or process step) to catch new code later recreating the exact pattern it swept for - the confidence is frozen at audit time while the codebase keeps moving.

## Project ground rules: `CLAUDE.md`

Check these too, on every run, in `src/` and `lib/`. Unlike the checklist above they are rules, not smells: flag every violation, still as a bare warning.

Why they exist, and the only two goals they serve:

- **Stability on the device**: an ESP32-C3 with about 380 KB of RAM, no PSRAM, a small stack and no exceptions. An out-of-memory, a stack overflow or an unaligned read crashes the reader.
- **Safety of the owner's data**: SD access is serialized, and errors are logged and handled rather than aborting.

The rules are the means, not the goal. If code breaks one in a case nobody anticipated and the result is clearly safer or clearer than following the rule, don't flag it as a violation: report it as `file[:line]: breaks rule N on purpose`, so the user can check the reasoning.

11. A bare `new` (not `new (std::nothrow)` or `makeUniqueNoThrow`), or an allocation whose null result is not checked and logged.
12. SD access through `SdFat`, `FsFile`, `FsBaseFile` or `SDCardManager` instead of `HalStorage` / `HalFile`.
13. An explicit `close()` on a local file handle (outside the close-before-remove and close-before-reopen cases).
14. A user-facing string not going through `tr()`.
15. A hardcoded screen size (800, 480) instead of `renderer.getScreenWidth()` / `getScreenHeight()`, or a raw `HalGPIO::BTN_*` outside `ButtonRemapActivity`.
16. A local buffer of 256 bytes or more on the stack.
17. A `uint8_t*` cast to a wider pointer type and dereferenced, instead of `memcpy`.
18. `std::function` or `std::string` / Arduino `String` in library code or a per-page / per-frame path; a `push_back` loop without `reserve`.
19. An error path that returns without `LOG_ERR`, an `abort()`, or an `assert` used for a recoverable condition.
20. A skipped or disabled test, a `cppcheck-suppress`, or a silenced warning.

## Output format

Plain markdown list, one line per finding: `file[:line]: should not <do Y>`.

- Include a line number only when a specific line makes sense (e.g. items 2, 3, 6); omit it for whole-file/structural smells (e.g. items 1, 4, 5, 7, 8).
- State the bare rule only. No "why", no failure scenario, no suggested fix, no fix direction.
- No severity ranking - flat list, in the order found.
- If nothing is found, say so plainly.

## When a finding doesn't fit the checklist

Report it the same way, prefixed `[off-checklist]`, so the reader can tell it apart.
