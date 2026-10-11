---
title: "Sync planner for GitHub library sync"
description: "Turn the manifest and the streamed remote tree into an ordered, deterministic list of moves, adoptions, downloads, deletes and prunes, within a fixed RAM workspace and a documented file-count cap, without ever deleting or overwriting what the sync does not own."
status: needs-triage
---

## Problem Statement

The owner presses **Update** so the SD card mirrors their private books repository (plan 1, § 1 Goal). After step 3 parses GitHub's tree listing and step 4 stores it as `tree.tmp` next to the sync's manifest, something has to decide **what to do**: which books are new, which changed, which were renamed or moved, which disappeared, which are already on the card from the PC copy of the first sync, and which folders became empty.

That decision is where the owner's books and reading state are at risk:

- A rename or a move pushed to GitHub must not turn into "delete the book, download it again": the download costs time and data, and the reading progress, bookmarks and recent-books entry are lost (plan § 2, "Moves and renames").
- A path the sync does not own must never be deleted: books copied by USB, OPDS or the web server, and anything under `/.crosspoint/` (plan § 2, "Deletion scope", "Filter").
- On the first sync, the files the owner already copied from a PC must be recognised instead of downloaded again (plan § 2, "First sync and adoption").
- The SD card is FAT: `Book.epub` and `book.epub` are the same file. A remote tree with both, or a rename that only changes case, must not overwrite one book with another, fail forever, or delete a file (plan step 5, "Verified by").
- The answer must be the same every time for the same inputs, so an interrupted sync resumes rather than flip-flops (plan § 2, "Atomic writes and resume": "the next run redoes only what is left").
- The device has about 380 KB of RAM and no PSRAM (CLAUDE.md). The full list of paths must not be held in RAM, the planning entries must be fixed-size SHA bytes plus a file offset, peak sync heap must stay at or below 64 KB, and the supported file count must be documented and enforced (plan § 2, "Streaming, not buffering").

Nothing in the codebase plans a sync today. `lib/GitSync/` holds the blob hasher (step 1), the path filter (step 2) and the trees parser (step 3); step 4's record format is being implemented now.

## Solution

A **sync planner** module in the `GitSync` library. It reads two step 4 record files through their stream interface, the compacted manifest and `tree.tmp`, keeps only fixed-size entries in a caller-allocated workspace of under 50 KiB, asks a `localExists` callback about the card where it must, and **streams** an ordered list of operations to a callback. It never hashes, never writes, never touches the card itself, and emits nothing unless both files were read completely and validly.

- Paths and full SHAs stay on the SD card. RAM holds one 12-byte slot per entry: the first 4 SHA bytes, a folded path key and the record's file offset packed with 8 state bits. Manifest entries and remote entries that are not unchanged share one array. Every prefix or key match is confirmed by reading the full record back by offset, so a prefix or hash collision can never produce a wrong operation. Whenever the planner needs a path (to confirm a match, to order a group, to ask `localExists`), it reads it back the same way.
- Path identity follows FAT: two paths are the same file when they are equal after the same Unicode upper-casing SdFat applies to long names. That one rule settles case-only renames (unchanged, no operation) and remote case collisions (one deterministic winner, the others skipped).
- Moves follow the plan's rule exactly: a remote path that matches no manifest path, whose SHA equals a manifest path matching no remote path. Several candidates sharing a SHA are paired in byte-wise lexicographic order of their paths.
- The supported library size is **2,000 synced files** (`MAX_SYNC_FILES`), guaranteed even when every file is moved in one push. A larger tree or manifest is refused before any operation is emitted, with both counts reported so the UI can say so. The RAM math is under Implementation Decisions.

## User Stories

1. As the library owner, I want an Update with no change on GitHub to do nothing, so that pressing Update is safe and fast.
2. As the library owner, I want a book I pushed to appear on the card, so that the card mirrors the repository.
3. As the library owner, I want a book I changed on GitHub to be downloaded again, so that the card holds the new version.
4. As the library owner, I want a modified book's stale cache to be cleared, so that the reader does not show the old layout; the planner tells the executor which manifest entry is replaced.
5. As the library owner, I want a book I deleted on GitHub to be deleted on the card, so that the card mirrors the repository.
6. As the library owner, I want a folder that only held synced books to disappear when those books are deleted or moved out, so that empty folders do not pile up.
7. As the library owner, I want a folder that still holds my own files to stay, so that the prune never removes anything I put there; the planner only asks for an attempt and the executor removes only empty folders.
8. As the library owner, I want a book renamed within a folder to be renamed on the card, not downloaded again, so that the sync is fast and my reading state follows the book.
9. As the library owner, I want a book moved to another folder to be moved on the card, so that the sync is fast and my reading state follows the book.
10. As the library owner, I want two books whose names I swapped to end up with the right content under each name, so that the card mirrors the repository even when the plan's move rule does not apply.
11. As the library owner, I want several identical files that I moved at once to be paired the same way on every run, so that an interrupted sync resumes instead of shuffling them.
12. As the library owner, I want the files I copied from my PC before the first sync to be checked and adopted, not downloaded, so that the first sync takes minutes instead of hours.
13. As the library owner, I want a remote path that does not exist on the card to be downloaded directly, so that the executor does not waste a hash pass on a missing file.
14. As the library owner, I want `.github/`, `.gitignore`, `.atelier/` and above all `.crosspoint/` in my repository to be ignored, so that the sync never writes into CrossPoint's own data.
15. As the library owner, I want a file the sync does not own never to be deleted, even if a remote file with the same SHA disappears, so that USB, OPDS and web-server books are safe.
16. As the library owner, I want a rename that only changes letter case to leave the book in place with no error, so that a cosmetic rename on GitHub costs nothing and never fails on FAT.
17. As the library owner, I want two remote files whose names differ only by case to produce one file on the card and a reported skip for the other, so that one book never overwrites the other on every run.
18. As the library owner, I want that skip to stay the same on every run without deleting anything, so that a naming mistake in my repository never costs me a book.
19. As the library owner, I want a remote file whose name FAT cannot store (`: * ? " < > |`) to be skipped and reported, so that it does not fail on every run.
20. As the library owner, I want a library larger than the supported size to be refused with both counts, so that I know why nothing happened and how far over I am.
21. As the library owner, I want a damaged, truncated or wrong-version manifest or tree file to stop the sync before anything happens, so that a half-read list never deletes books.
22. As the library owner, I want an SD read error during planning to stop the sync before anything happens, so that a flaky card never produces a partial plan.
23. As the library owner, I want an interrupted move (file renamed, reading state not yet migrated) to be finished on the next run, so that my progress is never lost; the planner still pairs the move when the source is gone and the destination exists.
24. As the library owner, I want a move whose source and destination are both missing on the card to become a plain download, so that the manifest never records a file that is not there.
25. As the library owner, I want a move whose destination already holds an unrelated local file not to rename over it, so that the executor verifies that file before keeping it.
26. As the library owner, I want the same inputs to give the same operation list on every run, whatever the order of entries in the files, so that a resumed sync redoes only what is left.
27. As an executor author (step 6), I want operations delivered one at a time through a function-pointer callback in the plan's order (moves, adopt-or-download, downloads, deletes, prunes), so that I can run each one as it arrives without a list in RAM.
28. As an executor author (step 6), I want each operation to carry record offsets rather than paths, so that I read the path, SHA and size from the record files with the step 4 by-offset reader.
29. As an executor author (step 6), I want a modify to carry both the tree entry and the manifest entry it replaces, so that I can clear the old path's cache and journal one replacing frame.
30. As an executor author (step 6), I want a move to say whether its source and destination existed when planned, so that I know whether to rename, only migrate, or verify the destination.
31. As an executor author (step 6), I want my callback to be able to stop the planner by returning false, so that Back cancels between operations.
32. As an executor author (step 6), I want the planner never to hash files, so that hashing stays an executor operation that can be cancelled and resumed.
33. As a firmware developer (step 9), I want `localExists` injected as a function pointer plus a context, so that the device maps it onto `Storage.exists` and the tests onto a case-insensitive fake.
34. As a firmware developer (step 9), I want the planner's whole RAM in one fixed-size object of documented size, so that I allocate it once with `makeUniqueNoThrow`, free it after the plan, and budget it against 64 KB.
35. As a firmware developer (step 9), I want a result with counts per operation kind and per skip reason, so that the progress screen can show `n/total` and the summary can show the counts.
36. As a test author, I want the planner to run on the host over step 4's in-memory streams, so that every scenario is a gtest with no SD card.
37. As a test author, I want the operation order fully specified, so that tests compare exact operation lists.
38. As a maintainer, I want the file-count cap, its RAM math and its rationale written next to the constant and in the plan's step notes, so that raising it later is a conscious decision.

## Implementation Decisions

### Modules

All in `lib/GitSync/`, with the conventions of steps 1 to 4: no namespace, `#pragma once`, `static constexpr` constants, no Arduino include, no `std::function`, no `std::string`, no heap inside the library. The planner does not log: it returns statuses and counts and the caller logs, like step 2's `classifySyncPath` and step 4's reader.

- **Path folding** (new, small, deep): the FAT path identity.
  - `syncPathKey(path, len)` returns a 32-bit FNV-1a hash of the path's upper-cased code points.
  - `syncPathsEqualFolded(a, aLen, b, bLen)` compares two paths code point by code point after upper-casing.
  - Upper-casing is a port of SdFat's `toUpcase` (BMP code points, its `mapTable` and `lookupTable`) as `constexpr` tables, MIT-licensed with attribution. SdFat compares long names with `toUpcase` (see Evidence), so "equal after folding" is exactly "the same file on the card". A port rather than a link, because host tests cannot compile SdFat, the same reason step 1 did not use mbedTLS.
  - UTF-8 is decoded to code points. A byte that is not part of a valid sequence, and any code point above U+FFFF, is compared as itself, unfolded; SdFat's `toUpcase` takes 16-bit values and leaves those unchanged.
  - `isFatSafeName(path, len)` rejects the FAT-forbidden printable characters `: * ? " < > |` and `\`. Control bytes, empty segments, `..`, a leading `/` and dot segments are already step 2's job.
- **Sync planner**: the class `SyncPlanner`, holding its workspace as fixed arrays, plus the operation and result types below.

### Inputs

- **Manifest**: a step 4 record source of kind manifest, **compacted** by step 6 at sync start, so it holds a sealed base and no journal (step 4 PRD, "Compactor contract" and "Persistence model"). A journal frame is reported as `ManifestNotCompacted`, never folded by the planner. A first sync passes an empty manifest (header plus seal, zero entries).
- **Tree**: a step 4 record source of kind tree (`tree.tmp`).
- **`localExists`**: `bool (*)(void* ctx, const char* path, size_t pathLen)`. The path is repository-relative and `'\0'`-terminated. The device maps it onto `Storage.exists("/" + path)`, which is case-insensitive on FAT; the test fake must be case-insensitive with the same folding. A false answer on an I/O error is acceptable: every use below falls back to a safe outcome (download or delete of a file GitHub still has).
- **Operation sink**: `bool (*)(void* ctx, const SyncOp& op)`. Returning false stops the planner, which returns `Aborted`.
- The planner never hashes, writes, renames or deletes anything. It only reads the two record sources and calls `localExists`.

### Path classification inside the planner

The planner re-applies the filters to every record even though step 9 filters before writing `tree.tmp`, because step 4's writer does not filter (H-4-9) and the plan's test feeds a `.crosspoint/…` entry.

- **Tree record**:
  - `classifySyncPath` not `Syncable` → ignored, counted (`hiddenIgnored` or `malformedIgnored`), no operation. `Hidden` covers `.crosspoint/…`.
  - Syncable but not `isFatSafeName` → a `Skip` operation with reason `UnsafeName`. It occupies a remote slot and counts toward the cap.
- **Manifest record**: a path that is not syncable (only possible from a manifest written by a broken build) is ignored and counted. It is never deleted, moved or pruned.

### Matching rules

Definitions, all case-insensitive in the FAT sense above unless stated:

1. **Unchanged**: a remote entry and a manifest entry with the same SHA and **byte-identical** paths. No operation.
2. **Path match**: a remote entry that is not unchanged, and a manifest entry that is not unchanged, whose paths are equal after folding.
   - Same SHA (a rename that only changes case): **no operation**. The file keeps its old case on the card and the manifest keeps the old path, which still opens the same file on FAT. A later modify writes the new case.
   - Different SHA: a **modify**, emitted as a `Download` that replaces that manifest entry.
3. **Remote case collision**: two or more remote entries whose paths are equal after folding. Exactly one **winner** is kept:
   - the one whose path is byte-identical to a manifest path, if any;
   - otherwise the byte-wise smallest path.
   The others become `Skip` operations with reason `CaseCollision`. The winner is then handled like any remote entry. Because the rule depends only on the inputs, and the winner is the one that lands in the manifest, every later run picks the same winner and emits the same skip: never an error, never a repeated download, never a deletion.
4. **Missing**: a manifest entry that is neither unchanged nor path-matched.
5. **New**: a remote entry that is not unchanged, not path-matched and not skipped.
6. **Move pairing**: for each SHA shared by missing and new entries, the missing paths and the new paths are each sorted byte-wise (`memcmp` order, a shorter prefix first) and paired by rank. Extra missing entries are deletes; extra new entries are handled as new.
7. **Move existence check**: each pair is checked with `localExists` on both paths.

   | Source on card | Destination on card | Outcome |
   |---|---|---|
   | yes | no | `Move` |
   | no | yes | `Move` with `sourceExists = false`, `targetExists = true`: an interrupted move whose migration is still due (plan step 6) |
   | yes | yes | not a move: `Delete` of the source, `AdoptOrDownload` of the destination, so an unrelated local file is hashed before it is kept |
   | no | no | not a move: `Delete` of the source (already gone), `Download` of the destination, so the manifest never records a file that is not there |

   Pairs are never re-formed after this check, so the result stays deterministic.
8. **New, not paired**: `localExists` true → `AdoptOrDownload`; false → `Download` with no replaced entry.
9. **Swap** of two files' names: both remote paths are manifest paths, so by rule 6 neither is a move (plan § 2 defines a move only for a remote path not in the manifest). The outcome is two modifies, each a `Download` replacing its own manifest entry, with the cache clear that implies. This is pinned by a test.
10. **Prune**: every `Delete` and every `Move` whose source path contains a `/` gets a `Prune` naming that manifest entry. A move whose source and destination share the same parent directory (compared folded) gets no prune. The executor walks the entry's ancestors deepest first and removes each directory only if it is empty, stopping at the first that is not, never the root. A prune is therefore an attempt; one per path is cheaper than tracking directories in RAM.

Consequences checked against the plan:

- A move destination is never a manifest path, and a move source is never a remote path, so moves never chain or cycle, and their order among themselves does not matter.
- Nothing outside the manifest is ever named by `Delete`, `Move` or `Prune`.
- Manifest invariant (step 6's responsibility, relied on here): no two manifest entries are equal after folding. Every path the executor records is a non-skipped remote path, and rules 1 to 3 guarantee those are fold-unique. If a broken manifest held two byte-identical paths, one would be planned as a delete and the next run would re-download it from GitHub: one wasted download, no lost data.

### Output

`SyncOp`, delivered by value through the sink, in this order:

| Kind | `manifestOffset` | `treeOffset` | Extra |
|---|---|---|---|
| `Move` | source entry | destination entry | `sourceExists`, `targetExists` |
| `AdoptOrDownload` | `NO_OFFSET` | remote entry | |
| `Download` | replaced entry (modify), or `NO_OFFSET` (new) | remote entry | |
| `Delete` | removed entry | `NO_OFFSET` | `sourceExists` is not checked: deletes do not call `localExists` |
| `Prune` | the deleted or moved-away entry | `NO_OFFSET` | |
| `Skip` | `NO_OFFSET` | remote entry | `reason`: `CaseCollision` or `UnsafeName` |

Order (pinned by tests, so that a test compares exact lists):

1. `Move`: by source manifest offset ascending.
2. `AdoptOrDownload`: by tree offset ascending (the file order of `tree.tmp`).
3. `Download`, new and modified together: by tree offset ascending.
4. `Delete`: by manifest offset ascending.
5. `Prune`: by manifest offset ascending.
6. `Skip`: by tree offset ascending. Skips are not in the plan's list; they come last so that informational entries never delay real work.

Moves before downloads, as the plan requires. Deletes before prunes, so a directory is tried only after everything planned inside it is gone.

**No operation is emitted before both files were read to `End` and every comparison and `localExists` call has been made.** All SD reads happen before the first emission, so an I/O error or a corrupt file found late can never leave a partial plan behind (plan § 2, "never half-applied"; step 4 PRD, "Caller rule").

`SyncPlanResult` carries the status, the per-kind operation counts, `hiddenIgnored`, `malformedIgnored`, `manifestEntries` and `treeEntries` (syncable entries, for the cap message), and, for a reader failure, which file and which step 4 reader status.

Statuses: `Ok`, `ManifestError`, `TreeError` (each with the reader status: truncated, corrupt, wrong version or I/O error, H-4-1), `ManifestNotCompacted`, `CapExceeded`, `Aborted`.

**Why a callback and not a vector or a file**: a growing `std::vector` of operations is what plan § 2 forbids, and an operations file would add a third record format and an SD write per operation. With a callback, step 6 can run each operation as it arrives (the planner's workspace then coexists with the executor, which the budget below allows), and step 9 can still write the operations to a file from the callback if it ever needs the workspace freed first.

### RAM layout and the file-count cap

All planning state lives in **one array of 12-byte slots** inside `SyncPlanner`, with capacity `2 × MAX_SYNC_FILES`. Manifest slots are loaded first; remote slots are appended after them, only for remote entries that are not unchanged.

| Field | Size | Meaning |
|---|---|---|
| `shaPrefix` | u32 | First 4 bytes of the blob SHA, assembled big-endian from bytes (no wide load from a byte buffer). Once a remote slot is path-matched or paired as a move destination, its SHA is no longer needed and this field holds the **linked manifest offset** instead (the replaced entry, or the move source). |
| `pathKey` | u32 | `syncPathKey` of the path: the folded FNV-1a hash. |
| `offsetAndState` | u32 | Low 24 bits: the record's file offset. High 8 bits: side (manifest or remote), a 3-bit state (manifest: unchanged, path-matched, missing, move source; remote: new, path-matched, move destination, `CaseCollision`, `UnsafeName`), and three flags (prune wanted, source exists, target exists). |

- **Why 24-bit offsets are enough**: a frame is at most 294 bytes (step 4), so 2,000 syncable entries take under 600 KB, far below 16 MiB. A record file whose offsets reach 2²⁴ (only possible with tens of thousands of ignored dot entries in `tree.tmp`) is refused with `CapExceeded`.
- **Why a 4-byte SHA prefix is safe**: the prefix only selects candidates. A match is acted on only after `readEntryAt` returns both full 20-byte SHAs (and, for "unchanged", both exact paths) and they compare equal. The plan's "SHA bytes plus a file offset" rule holds: the slot holds SHA bytes and an offset, and the full SHA is one read away.
- **Why no index links**: a link stores the manifest **offset**, not an array index, so the array can be re-sorted freely between passes.

Memory, at `MAX_SYNC_FILES = 2000`:

| Part | Size |
|---|---|
| Slot array: 4,000 × 12 B | 48,000 B |
| Path scratch, one path of up to `MAX_SYNC_PATH_BYTES` (255) plus `'\0'` | 256 B |
| Two step 4 readers (manifest, tree) in `std::optional`, re-emplaced per pass (H-4-6) | ~640 B |
| Counters and flags | < 100 B |
| **Total** | **≈ 49,000 B (≈ 47.9 KiB)** |

- A `static_assert` keeps `sizeof(SyncPlanner)` at or below **50 KiB (51,200 B)**, leaving about 2 KB of slack for H-4-6. If step 4's reader is bigger than assumed, the implementer lowers `MAX_SYNC_FILES` until the assert holds and records the value in the step notes.
- **Worst case covered**: when every file is renamed or moved, every manifest entry is missing and every remote entry is new, so both sides take a slot: 2 × 12 = **24 B per file**, 48,000 B at 2,000 files. A smaller cap is never needed for a reorganization, and no move detection is dropped.
- **Levers evaluated** (bytes per file in that worst case, and the cap that fits the ≈ 50,200 B left for slots):

  | Layout | B/file | Cap |
  |---|---|---|
  | Previous draft: 24 B manifest slot + 1 B state + 12 B remote slot | 37 | 1,300 |
  | 8-byte SHA prefix + key + offset (16 B), both sides | 32 | 1,568 |
  | 8-byte prefix + offset, key dropped (12 B), both sides | 24 | 2,091, but path matching would need a third pass with readbacks for every missing entry against every new entry: not linear |
  | **4-byte prefix + key + packed offset/state (12 B), both sides (chosen)** | **24** | **2,091; set to 2,000** |

  The 8-byte prefix was rejected because confirmation reads make the prefix length irrelevant to correctness, and the 4 freed bytes pay for the path key that keeps path matching linear.
- **The cap**: the planner refuses with `CapExceeded`, before any operation, when the manifest or the tree holds more than `MAX_SYNC_FILES` syncable entries (ignored dot paths do not count; `UnsafeName` skips do). Both counts are in the result.
- **Allocation**: the caller allocates the planner once per sync with `makeUniqueNoThrow<SyncPlanner>()`, checks for null and logs `LOG_ERR` on failure, and frees it after the plan. Static storage was rejected because it would hold 48 KiB of DRAM permanently for a feature used occasionally; the stack is out of the question. No allocation happens inside the planner.
- **Budget**: the planner's ≤ 50 KiB coexists with the executor running inside the callback, which leaves the executor and glue at least 14 KiB of the 64 KB (TLS excluded by the plan). The emit-through-callback model is unchanged.

### Passes and SD readbacks

The implementer may reorganize these, as long as the outcomes, the order, the memory and the readback bounds hold.

1. **Load the manifest** sequentially (cap → `CapExceeded`; journal frame → `ManifestNotCompacted`). Each slot gets prefix, key, offset, and "nested" (path has a `/`). Sort the manifest slots by (prefix, key).
2. **Stream the tree.** For each syncable record, binary-search (prefix, key) among manifest slots. For each candidate, one `readEntryAt` on the manifest confirms full SHA and byte-identical path → manifest slot `unchanged`, no remote slot. Otherwise append a remote slot (unsafe names marked `UnsafeName`). Because the key is part of the search, identical files at different paths do not become candidates of each other: about one readback per remote entry.
3. **Path matching and collisions.** Sort all non-unchanged slots plus unchanged manifest slots by key. In each run of equal keys, confirm folded equality by readback (one path in the scratch). Apply rules 2 and 3: a remote slot losing to an unchanged or exact manifest path, or to a smaller remote path, is `CaseCollision`; a path match compares full SHAs by readback (same → both done, case-only rename; different → remote slot stores the manifest offset as the replaced entry).
4. **Pair moves** (only when both missing and new slots exist). Sort missing and new slots by prefix. In each run of equal prefixes, read each entry's full SHA once to split the run by true SHA. Within a true-SHA group, order each side by path with `std::sort` and a comparator that reads paths (an I/O error sets a flag checked after the sort), then pair by rank. A paired remote slot stores the source offset. Then run the `localExists` checks of rules 7 and 8, and the same-parent check for the prune flag.
5. **Emit** in the documented order, re-sorting by (kind, offset). No SD read happens in this pass.

Readback bounds, pinned by a seek-counting test:

- No change, or changes without moves: at most one `readEntryAt` per remote entry in pass 2 plus at most two per slot in a key run in pass 3, so the number of seeks stays at most **3 × entries + a constant**. Equal keys of different paths (a 32-bit hash collision) add reads only for those slots.
- Moves: one extra read per slot in a prefix run, plus O(g log g) path reads for a group of g identical files moved together. A no-change or ordinary-change run stays linear.

## Implementation Chunks

1. **Path folding.** `syncPathKey`, `syncPathsEqualFolded`, `isFatSafeName` and the ported upcase tables. Tests: ASCII and Latin-1 pairs (`Été` vs `ÉTÉ`, `é` vs `É`), Greek and Cyrillic pairs from the table, `ß` unchanged, invalid UTF-8 bytes and 4-byte sequences compared unfolded, key equality whenever folded equality holds, every forbidden character rejected, nested UTF-8 names accepted.
2. **Planner core.** Workspace, the two readers, statuses, manifest load, tree stream, unchanged and path matches (including case-only rename), new entries with `localExists`, deletes and prunes, ignored dot paths, the cap, the "nothing emitted before the end" rule, sink abort, and the operation order for the kinds handled so far. Tests: the plan's no-change, add, modify, delete-plus-prune, adopt-or-download, `.crosspoint/` and cap scenarios, plus reader failures and I/O errors.
3. **Moves and collisions.** Prefix-run splitting by full SHA, move pairing with lexicographic order, the existence table, prune suppression within one folder, remote case collisions and `UnsafeName` skips, swap. Tests: the plan's rename, move, swap, same-SHA, case and collision scenarios, the existence table, determinism under shuffled input order, and the SD-read bound.

## Testing Decisions

- **What makes a good test**: only the public interface. Each test builds a manifest and a tree with step 4's writer into step 4's in-memory stream (H-4-7), runs the planner with a fake `localExists` and a recording sink, and compares the exact list of operations. Offsets in the expected list come from the offsets the writer reported for each `put`, never from computing the layout. A test helper may resolve offsets to paths through step 4's reader so a failure message prints paths.
- **Fake `localExists`**: a set of repository-relative paths looked up with `syncPathsEqualFolded`, so the fake behaves like FAT.
- **Suite**: `test/git_sync_planner/`, registered with `add_subdirectory` in `test/CMakeLists.txt`, laid out like `test/git_sync_path_filter/` and `test/git_sync_tree_parser/`. It compiles the planner, the folding module, step 2's `SyncPath`, and step 4's record sources (H-4-13), with the `lib/GitSync` include directory, `crosspoint_test_common` and `GTest::gtest_main`. No logging stub is needed, since none of these modules log, unless step 4's sources pull in step 3's `Logging.h` (then reuse that suite's stub).
- **The plan's scenarios, with their pinned outcomes**:
  - no change → no operation, `Ok`;
  - an add, not on the card → `Download` with no replaced entry;
  - a modify → `Download` replacing the manifest entry;
  - a delete in a folder → `Delete` then `Prune` of the same entry; a delete at the root → `Delete` only;
  - a rename within a folder → `Move`, no `Prune`; a move across folders → `Move` then `Prune` of the source;
  - a swap of two files' names → two `Download`s, each replacing its own entry, in tree order, no `Move`;
  - two missing paths with the same SHA and two new paths with that SHA → two `Move`s, paired by byte-wise order; two missing and one new → one `Move` from the smaller source path, and a `Delete` (plus `Prune` when nested) of the other;
  - a remote path not in the manifest that exists on the card → `AdoptOrDownload`;
  - a remote `.crosspoint/…` entry (and `.github/…`, `a/.git/b`) → no operation, counted as ignored, not counted toward the cap;
  - a rename that only changes case, same SHA → no operation; same path folded with a different SHA → `Download` replacing the old entry;
  - a remote tree with `Book.epub` and `book.epub`, neither in the manifest → the byte-wise smaller (`Book.epub`) is downloaded or adopted, the other is a `Skip` (`CaseCollision`); rerun with the winner now in the manifest → only the same `Skip`, no `Delete`, no `Download`; with `book.epub` in the manifest instead → `book.epub` wins;
  - a tree of `MAX_SYNC_FILES` (2,000) syncable entries → planned; one more → `CapExceeded` with both counts and no operation; the same for the manifest; extra dot entries do not trip the cap;
  - the worst case at the cap: a manifest of 2,000 entries and a tree of the same 2,000 contents all at new paths → 2,000 `Move`s, no `CapExceeded`.
- **Additional pinned outcomes**:
  - the four rows of the move existence table;
  - a remote name with each FAT-forbidden character → `Skip` (`UnsafeName`), the rest of the plan unaffected;
  - order: a mixed scenario with every kind produces exactly the documented order;
  - determinism: the same entries written to the files in a different order produce the same pairing and the same operations (orders keyed on offsets follow the new offsets);
  - safety: no `Delete`, `Move` or `Prune` ever names an entry that is not in the manifest, checked over every scenario;
  - failures: a truncated, corrupt or wrong-version manifest or tree, a tree read as a manifest, a manifest with a journal frame, and a source failing its N-th read for every N during a planning run, each give their status and **zero** operations;
  - the sink returning false on its K-th call stops the planner with `Aborted` after exactly K calls;
  - SD-read bound: with a counting source, a no-change plan and a plan with only adds, modifies and deletes over `MAX_SYNC_FILES` entries perform at most 3 × entries plus a constant seeks, so a quadratic lookup is caught; the plan holds with many identical files (same SHA at different paths);
  - prefix collisions: two different SHAs sharing their first 4 bytes, crafted in the test, in each role (unchanged check, path match, move pairing), never produce an unchanged, a case-only no-op or a move between them;
  - memory: `sizeof(SyncPlanner) <= 51200` is a `static_assert`, and the tests at the cap run with the planner on the heap.
- **Prior art**: `test/git_sync_path_filter/` (table tests, layout), `test/git_sync_tree_parser/` (scenario fixtures, every-boundary loops), step 4's `test/git_sync_record_format/` (writer into memory, fault-injecting sources).
- **Run with**: `cmake -S test -B build/test -G Ninja && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`.
- **Not tested here**: the device `localExists`, executing the operations, and wall-clock time on a real card (steps 6, 9 and the owner's step 12 checklist).

## Out of Scope

- Executing operations: renames, downloads, hashing for adoption, cache clears, reading-state migration, directory removal, manifest journaling and compaction (step 6).
- The `HalFile` adapter, `Storage.exists` wiring, file names on the card, writing `tree.tmp` and applying the filter before it (step 9).
- User-facing strings for the cap and skip messages (step 10).
- Copy detection (one manifest file now present at two remote paths): the extra path is downloaded.
- Making the card's letter case follow a case-only rename: the file keeps its old case until its content changes.
- A remote file whose path folds onto a remote directory (`A` as a file and `a/b`): the executor reports a per-file error; this cannot happen with byte-identical paths in git and is not detected here.
- Raising the cap beyond 2,000 (the 50 KiB planner budget allows about 2,091).
- Any change to steps 1 to 4's modules.

## Further Notes

### Evidence

- Plan step 5 text and "Verified by" (`docs/plan-1.md`, § 3, Phase 1, step 5).
- Plan § 2: "Moves and renames" (move rule, lexicographic pairing, modify clears cache), "Deletion scope" (manifest only, prune only where a synced file lived), "First sync and adoption", "Filter", "Streaming, not buffering" (SHA plus offset entries, 64 KB, documented cap), "Atomic writes and resume".
- Plan § 5, "Library size is unknown": flag a cap below about 2,000 files.
- Plan § 6, step 1.1 (`GitBlobHasher::SHA_SIZE = 20`), step 1.2 (`MAX_SYNC_PATH_BYTES = 255`; "FAT-forbidden printable characters and case clashes stay with steps 5/6"), step 1.3 (records usable only when complete; the parser does not filter).
- `lib/GitSync/SyncPath.h` (`classifySyncPath`, `MAX_SYNC_PATH_BYTES`) and `lib/GitSync/GitBlobHasher.h` (`SHA_SIZE`), at HEAD.
- `lib/hal/HalStorage.h:50` (`exists`), `:52` (`rename`); `src/activities/reader/ProgressFile.h:53` (SdFat rename does not overwrite).
- `src/activities/reader/EpubReaderActivity.cpp:127-146` (`moveFinishedBookToReadFolder`, the move migration the executor follows).
- SdFat, as resolved by PlatformIO: `src/FatLib/FatFileLFN.cpp:79` compares long names with `toUpcase`; `src/common/upcase.h:27` declares `uint16_t toUpcase(uint16_t)`; `src/common/upcase.cpp` holds the tables to port.
- CLAUDE.md: 380 KB RAM, "Stack Safety" (256 bytes), "Heap Fragmentation", "`constexpr` First", "`std::vector` Pre-allocation", "Template and `std::function` Bloat", "RISC-V Alignment" (slots are naturally aligned structs; nothing is cast from a byte buffer), `makeUniqueNoThrow`.
- Step 4 PRD (`docs/prd/triage/phase1-step4-record-format.md`): "Reader contract", "By-offset read", "Compactor contract", "Guidance for steps 5 and 6", "Memory".

### Guidance for step 6 (not requirements of this step)

- Run each operation inside the sink callback, or write it to a file; either way keep the executor's own heap at or below 14 KiB while the planner is alive.
- `Move` with `sourceExists = false` and `targetExists = true`: hash the destination first (as adopt does) and migrate the reading state only when it matches; otherwise download.
- `Prune`: walk the entry's ancestors deepest first, `rmdir` only when empty, stop at the first failure, never the root and never under `.crosspoint/`.
- `Download` with a replaced entry: clear the cache of the **manifest** path (the old name), and journal one frame with `replaces` set.
- Treat `Skip` as a log line and a summary count, never as an error.
- Keep the manifest invariant the planner relies on: only non-skipped remote paths are ever recorded.

### Verification

- **Host**: the new suite passes under the `ctest` command above.
- **Firmware**: `pio run` still succeeds; nothing in `src/` uses the planner yet, so the image is unchanged. Format with `./bin/clang-format-fix -g`.
- **Step notes**: record `MAX_SYNC_FILES`, the measured `sizeof(SyncPlanner)` and the 24 B-per-file worst case. The cap meets the plan's ~2,000 (§ 5), so no owner decision is needed.
- **Device** (owner, step 12): the first sync's duration with the library's file count, and free heap at or above 50 KB during a sync.

### Hypotheses

Step 4 (manifest and tree record format) is being implemented while this PRD is written. These assumptions rest on its PRD (`docs/prd/triage/phase1-step4-record-format.md`) and the committed tree only. The implementer checks each against the committed step 4 code before starting, and adapts names without changing the outcomes above.

- **H-4-1**: The reader's `next(record)` returns one of `Record`, `End`, `Truncated`, `Corrupt`, `WrongVersion`, `IoError`, and keeps returning a non-`Record` status once reached. The planner maps any non-`End` final status to `ManifestError` or `TreeError` carrying that status.
- **H-4-2**: The reader is constructed from a source and the expected file kind (tree or manifest). A file of the other kind reads `Corrupt`, and an empty manifest (header plus seal) reads `End` with no record.
- **H-4-3**: A record carries its frame kind, its offset (u32, at least 8; the planner stores it in 24 bits and refuses larger), whether it is in the journal, and for an entry `sha[20]`, `size` (u64), `replaces`, a `'\0'`-terminated `path` and `pathLen`, valid until the next call on that reader. `readEntryAt(offset, record)` returns the same record for an entry frame, requires the file to have been read to `End` by the same caller, and leaves the sequential position undefined.
- **H-4-4**: `NO_OFFSET = 0xFFFFFFFF` is a public constant the planner reuses in `SyncOp`.
- **H-4-5**: A compacted manifest holds no journal frame. A record with the journal flag set, or a drop frame, means the caller did not compact; the planner reports `ManifestNotCompacted` rather than folding the journal.
- **H-4-6**: A reader allocates nothing and is about 320 bytes, so two readers in `std::optional` members keep `sizeof(SyncPlanner)` within 50 KiB at 2,000 files (about 2 KB of slack). The reader can be re-emplaced for each pass; it needs neither copy nor move.
- **H-4-7**: Step 4 provides a header-only in-memory stream over a caller-owned buffer implementing both the source and the sink, and a writer with `begin(kind)`, `put(sha, size, path, pathLen, replaces, offsetOut)` and `seal()`, so tests build both files and learn each entry's offset.
- **H-4-8**: Paths in record files are at most `MAX_SYNC_PATH_BYTES` (255) bytes, enforced by the writer and the reader, so the 256-byte scratch always holds one with its terminator.
- **H-4-9**: The writer does not filter paths, so a test can put `.crosspoint/x` or a forbidden character into a tree file and the planner must filter it itself.
- **H-4-10**: The source interface is `int read(uint8_t*, size_t)` and `bool seek(uint32_t)`; a test subclass can fail on its N-th call or count seeks, as step 4's own tests do.
- **H-4-11**: Reader statuses distinguish `IoError` (transient) from `Corrupt`, so the result can tell the UI "retry" from "delete the manifest".
- **H-4-12**: Step 6 compacts at sync start and passes the compacted manifest; base offsets do not move while the planner and the executor run, so the offsets in `SyncOp` stay valid while the executor appends journal frames.
- **H-4-13**: Step 4's sources and suite are named as its PRD suggests (record format sources in `lib/GitSync/`, suite `test/git_sync_record_format/`); the planner suite compiles whichever source files step 4 actually committed.
