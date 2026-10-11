---
title: "Manifest and tree record format for GitHub library sync"
description: "The sync needs one SD-card file format for the remote tree listing and for its own manifest, readable sequentially and by record offset with bounded RAM, cheap to update after every operation, and that reports a truncated, corrupt or wrong-version file instead of applying part of it."
status: needs-triage
---

## Problem Statement

The owner presses **Update** so the SD card mirrors their private books repository (plan 1, § 1 Goal). Two lists drive every sync, and neither fits in RAM (plan § 2, "Streaming, not buffering": no full path list in RAM, fixed-size planning entries of SHA plus file offset, peak sync heap at most 64 KB):

- **The remote tree**, streamed from GitHub by the step 3 parser and written as records to `/.crosspoint/gitsync/tree.tmp`.
- **The manifest**, the sync's memory of every path it owns with its blob SHA. It is the only thing that allows a deletion (plan § 2, "Deletion scope"), and it must be "updated and persisted after each applied operation" so that an interrupted run leaves it consistent with the files on disk (plan § 2, "Atomic writes and resume").

Nothing in the codebase stores either list today. The existing cache serialization (`lib/Serialization/Serialization.h:8-25`) writes raw PODs through `reinterpret_cast` with no checksum and no way to tell a cut-off file from a complete one, so it cannot serve here.

The failures that matter to the owner:

- A `tree.tmp` cut off by a Wi-Fi drop or power loss must never be read as the full repository: the planner would treat the missing paths as removed and delete books.
- A manifest damaged on the card must be reported, never used in part: a half-read manifest can make the sync forget it owns files or misidentify a move.
- Persisting the whole manifest after each of N operations writes O(N²) bytes. At 2,000 files of about 100 bytes per record, the adoption pass of the first sync would rewrite 200 KB 2,000 times, about 400 MB of SD writes. Plan step 6 measures bytes written at the file-count cap to catch exactly this.
- Multi-byte fields read from a byte buffer fault on the ESP32-C3 when unaligned (CLAUDE.md, "RISC-V Alignment").

## Solution

A versioned **record file** format in the `GitSync` library, with a writer, a sequential reader, a by-offset reader and a manifest compactor, all running over two tiny stream interfaces (read-and-seek, write). The host tests use an in-memory stream. On the device (step 9), a trivial adapter forwards to `HalFile`.

- The **same format** serves both files. Each file starts with a header naming its kind (tree or manifest) and the format version, then a sequence of checksummed **frames**.
- A **tree file** is a list of entry records closed by a **seal** that carries the record count. A tree file without its seal (a stream that never finished, or a cut-off file) is reported as truncated, never as a shorter tree.
- A **manifest** is a sealed **base** (the entries at the start of the run), followed by an append-only **journal** of single-frame operations, one per applied sync operation: "this path now has this SHA, replacing the entry at offset X" or "the entry at offset X is gone". Persisting an operation costs one append of about 40 bytes plus the path, so a run writes bytes linear in its operation count. A journal frame cut off by a crash is recognised as the torn tail and ignored, exactly as if the crash had happened before that append.
- At the start of every sync, step 6 **compacts** the manifest into a fresh base (written to a temporary file, then remove and rename) and drops the torn tail. The planner then reads a base-only manifest, and every offset it holds stays valid for the rest of the run.
- Every damaged, truncated, wrong-kind or wrong-version file produces a status the caller acts on before using anything it read. Wrong version is its own status.
- Every multi-byte field is decoded byte by byte from the frame buffer. No buffer pointer is ever cast to a wider type.

## User Stories

1. As the library owner, I want an interrupted tree download never to be read as my whole repository, so that a Wi-Fi drop never deletes my books.
2. As the library owner, I want the manifest saved after each file the sync handles, so that pressing Update again after an interruption redoes only what is left.
3. As the library owner, I want saving the manifest to cost only the operation just done, so that the first sync over a large library does not spend minutes rewriting the same file and wearing the card.
4. As the library owner, I want a crash in the middle of a manifest save to lose at most that one operation's record, so that the next run sees a state it can recover from.
5. As the library owner, I want a damaged manifest to stop the sync with an error rather than be partly used, so that the sync never deletes or moves books based on a half-read list.
6. As the library owner, I want a manifest from an incompatible firmware version to be recognised as such, so that an update never misreads my sync state.
7. As the library owner, I want accented and non-Latin paths stored byte for byte, so that a book named `Livres/Été.epub` is tracked under its real name.
8. As the library owner, I want empty files and files over 4 GiB stored with their exact size, so that the size check of each download compares against the true value.
9. As a firmware developer (step 9), I want the stream interfaces to match `HalFile`'s `read`, `seek` and `write`, so that the device adapter is a few forwarding lines.
10. As a firmware developer (step 9), I want to write `tree.tmp` record by record as the parser emits them, and seal it only when the parser reports a complete, non-truncated response, so that the file on the card proves the tree was complete.
11. As a firmware developer (step 9), I want the writer to stop with an error on a short write, so that a full card cannot produce a file that looks valid.
12. As a planner author (step 5), I want each record's byte offset reported when it is read, so that my fixed-size RAM entries can hold an SHA plus an offset instead of a path.
13. As a planner author (step 5), I want to read a record's path back from its offset, checked against its checksum, so that I can compare, sort or report paths without holding them in RAM.
14. As a planner author (step 5), I want a manifest I read after compaction to contain only base records, so that a manifest offset I hand to the executor stays valid for the whole run.
15. As an executor author (step 6), I want to append "path P now has SHA S, replacing the entry at offset X" as one frame, so that a modify or a move is persisted atomically, never as a separate drop and add.
16. As an executor author (step 6), I want to append "the entry at offset X is gone" as one frame, so that a delete is persisted atomically.
17. As an executor author (step 6), I want a compactor that folds the journal into a fresh base and reports the live entry count, so that each run starts from a clean, sealed manifest.
18. As an executor author (step 6), I want the compactor to use a scratch buffer I provide, so that its RAM fits my budget and the library never allocates.
19. As an executor author (step 6), I want the compactor to refuse a journal that drops or replaces an entry twice, or one that does not exist, so that a corrupt journal never produces a wrong manifest.
20. As an executor author (step 6), I want the reader to tell me whether it ignored a torn tail, so that I can log it.
21. As an executor author (step 6), I want a missing manifest to be distinguishable from a corrupt one, so that a first sync starts from an empty manifest while a damaged one stops the sync.
22. As a firmware developer, I want reading a file from a different kind (a manifest opened as a tree, or the reverse) to be reported, so that swapped files are never misused.
23. As a firmware developer, I want all fields decoded without unaligned loads, so that the code is safe on the RISC-V target (CLAUDE.md, "RISC-V Alignment").
24. As a firmware developer, I want the reader and the writer to allocate nothing and have a documented size, so that I can budget them inside the 64 KB sync heap and keep them off the task stack.
25. As a firmware developer, I want the format documented in `docs/file-formats.md` with its version, so that a future format change bumps the version (CLAUDE.md, "Cache File Format Versioning").
26. As a test author, I want an in-memory stream over a caller-owned buffer, so that the planner and executor tests (steps 5 and 6) can build and inspect record files without an SD card.
27. As a test author, I want every byte-offset truncation of a valid file to give a defined result, so that crash safety is proven exhaustively, not by example.

## Implementation Decisions

### Modules

All in `lib/GitSync/`, with the step 1 and step 2 conventions: no namespace, `#pragma once`, `static constexpr` constants, no heap, no Arduino include, **no logging** (statuses are returned and the caller logs, like step 2's `classifySyncPath`).

- **CRC-32**: the IEEE 802.3 reflected CRC-32 (polynomial `0xEDB88320`, initial value and final XOR `0xFFFFFFFF`) with an incremental form `update(crc, bytes, len)` and a one-shot form. Its 256-entry table is `constexpr`, so it is computed at compile time and stays in flash (1 KB, CLAUDE.md "constexpr First"). It is not borrowed from miniz, for the same reason step 1 did not use mbedTLS: the host tests would have to compile miniz.
- **Stream interfaces**: two abstract classes with virtual methods. A virtual call per frame is negligible next to SD I/O, and it avoids both templates and `std::function` (CLAUDE.md, "Template and `std::function` Bloat").
  - Source: `int read(uint8_t* buf, size_t len)`, which returns the bytes read (0 at end of file, fewer than `len` near it) or a negative value on I/O error, as `HalFile::read` does (`lib/hal/HalStorage.h:101`); and `bool seek(uint32_t offset)`, mapping to `HalFile::seek(size_t)` (`:95`).
  - Sink: `bool write(const uint8_t* buf, size_t len)`, which returns true only when all `len` bytes were written. The adapter compares `HalFile::write`'s return value (`:103`) with `len`.
- **In-memory stream**: header-only, over a caller-owned byte buffer with a capacity and a current length. It implements both interfaces; a write past capacity fails. No heap, so it is safe in `lib/` and reusable by the step 5 and step 6 tests. Fault injection (partial writes, I/O errors) stays in each test's own subclass.
- **Record writer**: writes a header, then frames, through a sink.
- **Record reader**: reads frames sequentially from a source, and reads one entry back by offset.
- **Manifest compactor**: folds a manifest's base and journal into a fresh sealed base, written to a sink.

### On-disk layout (format version 1)

All integers are little-endian and written byte by byte. There is no padding and no packed struct.

File header, 8 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `GSRF` |
| 4 | 1 | format version, `RECORD_FILE_VERSION = 1` |
| 5 | 1 | file kind: 1 = tree, 2 = manifest |
| 6 | 2 | reserved, must be 0 |

Frame, 7 bytes followed by the body:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | frame kind: `'P'` entry, `'D'` drop, `'S'` seal |
| 1 | 2 | body length |
| 3 | 4 | CRC-32 over the frame kind, the body length bytes and the body |
| 7 | n | body |

The CRC sits before the body, so the path is the last thing in the frame buffer and the reader terminates it in place, without a second path buffer.

Bodies:

- **Entry (`'P'`)**: `sha[20]`, `size` u64, `replaces` u32, then the path bytes, with no terminator. The path length is the body length minus 32. It must be between 1 and `MAX_SYNC_PATH_BYTES` (255, from step 2's `SyncPath.h`). `replaces` is `NO_OFFSET = 0xFFFFFFFF` or the offset of an earlier entry frame. The largest frame is 7 + 32 + 255 = 294 bytes.
- **Drop (`'D'`)**: `target` u32, the offset of an earlier entry frame.
- **Seal (`'S'`)**: `count` u32, the number of entry frames before it.

An **offset** is the byte position of a frame's first byte from the start of the file. Valid offsets are therefore at least 8. Offsets are u32, which covers any file on FAT32.

Grammar, by file kind:

- **Tree**: header, then entries with `replaces = NO_OFFSET`, then exactly one seal whose count matches, then end of file.
- **Manifest**: header, then base entries with `replaces = NO_OFFSET`, then one seal whose count matches. After that comes the journal: any mix of entry frames (with `replaces` set or `NO_OFFSET`) and drop frames, and no further seal.

### Reader contract

- The reader is built on a source and the expected file kind. `next(record)` returns a status:
  - `Record`: an entry or a drop. The record carries the frame kind, its offset, whether it is in the journal, and either sha / size / replaces / path / pathLen or the drop target. The path is terminated and valid until the next call.
  - `End`: the file is valid and fully read.
  - `Truncated`: the file ends before the grammar allows it to.
  - `Corrupt`: bad magic, wrong file kind, non-zero reserved bytes, an unknown frame kind, a body length that does not fit the frame kind, a path length out of range, a CRC mismatch, a seal count mismatch, a frame after the seal in a tree, a seal in a journal, a drop or `replaces` in a tree or a base, or a drop or `replaces` target that is not below the frame's own offset or is below 8.
  - `WrongVersion`: the magic is right but the version is not 1. It is reported before anything else is read.
  - `IoError`: the source returned an error. This is a transient failure, not a judgement on the file.
- Once `next` has returned anything other than `Record`, it keeps returning that status.
- **Torn tail, manifest journal only.** If the last journal frame is incomplete (the file ends inside its header or body), or it is complete but fails its CRC and the file ends right after it, the reader returns `End` and sets a `tornTail()` flag. It reports the offset where the valid content ends. A CRC failure followed by more bytes is `Corrupt`. A defect before the end of the seal is never a torn tail.
  - **Why this is safe**: the writer emits each operation as one frame, so ignoring the torn frame is the same state as a crash just before that append. Step 6 must already recover from that state, because the plan requires recovery from an interruption between a file operation and its manifest persist.
- **Truncation outcomes**, which are what the exhaustive tests pin down. Take a valid file cut to its first `k` bytes:
  - A tree file never reaches `End` for any `k` shorter than its full length.
  - A manifest never reaches `End` when `k` is before the end of its seal.
  - For any `k` at or after the end of the seal, a manifest reaches `End` with exactly the journal frames that fit completely in `k` bytes. `tornTail()` is true when `k` falls inside a frame.
- **Caller rule ("never half-applied")**, documented on the interface: records read before a non-`End` status must be discarded. The planner keeps its fixed-size entries but plans nothing until `End`. The compactor writes into a sink that its caller discards on failure. The reader streams; it does not pre-validate in a separate pass, because the planner is side-effect-free until it has the whole list, and the compactor needs two passes anyway.
- **By-offset read**: `readEntryAt(offset, record)` seeks to the frame, reads it, checks its CRC and body length, and requires an entry frame. It returns `Record`, `Truncated`, `Corrupt` or `IoError`. Precondition, documented: the file was already read to `End` by the same caller. `readEntryAt` checks the frame, not the whole file. After `readEntryAt`, the sequential position is undefined; a caller that wants to continue scanning starts a new reader.
- **No header re-check per by-offset read.** It would cost an extra seek and read for every lookup, and the precondition already covers it.

### Writer contract

- `begin(kind)` writes the header. `appendTo(endOffset)` resumes a manifest's journal at a known end offset, the file size after a clean read, without writing a header. In journal mode only entry and drop frames are allowed.
- `put(sha, size, path, pathLen, replaces, offsetOut)` writes one entry frame and returns its offset. `drop(target)` writes one drop frame. `seal()` writes the seal with the running entry count. In journal mode, `seal()` is refused.
- The writer enforces the grammar it can see: a path that is empty or longer than `MAX_SYNC_PATH_BYTES`, a `replaces` or drop outside a manifest journal, a target that is not below the current offset or is below 8, and any frame after a tree's seal are all refused. A refused call writes nothing and returns false.
- Each frame is assembled in the writer's buffer and passed to the sink in **one** `write` call, so a tear can only cut that one frame.
- A failed sink write makes the writer **sticky-failed**: every later call returns false. `failed()` reports it.
- **No path filtering in the writer.** It only checks the length. Deciding what may be synced is step 2's job, and the caller applies it before `put` (H-3-2).

### Compactor contract

- Input: a manifest source; an output sink; a caller-provided `uint32_t` scratch array and its capacity.
- **Pass 1** reads the whole manifest. It collects every drop target and every `replaces` value into the scratch array, then sorts it with `std::sort`, a single `uint32_t` instantiation. A duplicate target is `Corrupt`. More targets than the capacity is a distinct `ScratchFull` status. Any reader status other than `End` is passed through.
- **Pass 2** seeks the source back to 0. It writes a fresh manifest header, then every entry whose offset is not a target (found by binary search), in file order, with `replaces = NO_OFFSET`, then the seal.
- **Dangling targets**: if the number of entries matched against targets differs from the number of targets, a target pointed at nothing and the result is `Corrupt`. The sink's contents are then garbage, and the caller discards them.
- **Result**: `Ok` with the live entry count and whether a torn tail was dropped, or the failing status. A sink failure gives `WriteFailed`.
- **Atomicity is the caller's job** (step 6): compact into `/.crosspoint/gitsync/` scratch space, then remove and rename over the manifest, as `ProgressFile::writeAtomic` does (`src/activities/reader/ProgressFile.h:53-56`). The in-memory storage of step 6 already models remove then rename.
- **Scratch size**: at most one target per journal frame, and a run appends at most one frame per planned operation. Step 6 therefore sizes the scratch array at the file-count cap that step 5 sets (4 bytes per file, 8 KB at 2,000 files), allocated with `makeUniqueNoThrow` and freed after compaction.
- **Record order**: the order of live entries in the output is not sorted by path. A consumer that needs an order (the planner's lexicographic pairing of moves, plan § 2) sorts its own fixed-size entries.

### Persistence model (decided here; the plan delegates it)

The manifest is **append-journaled with a compaction at the start of each sync**, not rewritten whole after each operation.

- **Why**: plan § 2 requires a persist after every applied operation, and plan step 6 measures bytes written at the file-count cap precisely to catch a cost quadratic in the file count. A whole-file rewrite costs about N × manifest size per run. An append costs one frame per operation (about 40 bytes plus the path; about 100 bytes for a 60-byte path). A full first-sync adoption at 2,000 files therefore writes about 200 KB of journal plus one compaction, instead of about 400 MB.
- **Why one frame per operation**: a modify or a move is a single entry frame with `replaces` set, never a drop followed by an add. A tear can then never leave the manifest without both the old and the new entry.
- **Why compact at start, not at end**: compacting at the start also clears a torn tail left by the previous run before anything is appended after it. HalFile has no truncate, so appending after garbage would otherwise make the next read `Corrupt`. It also gives the planner a base-only manifest whose offsets do not move during the run. Compacting again at the end of a successful run is allowed but not required. That is step 6's call.
- **Rejected**: a base file plus a separate journal file (two files to keep in step, and the planner's offsets would span two files); in-place tombstone bytes (a sub-sector rewrite of an existing frame, which the CRC cannot cover and FAT does not make atomic); dedup by path during compaction (it needs a path hash table in RAM, while offset targets need only 4 bytes per operation).

### Memory

- **Reader**: the 294-byte frame buffer plus one terminator byte, plus a few counters and flags, about 320 bytes.
- **Writer**: the 294-byte frame buffer plus a few counters, about 310 bytes.
- **Compactor**: one reader and one writer as members, about 640 bytes, plus the caller's scratch array.
- None allocates. All are over CLAUDE.md's 256-byte stack guideline, so the device glue holds them as members or through `makeUniqueNoThrow`, never as locals. Host tests may keep them on the stack.
- Frame reads take two source reads (the 7-byte frame header, then the body). Buffering the `HalFile` adapter is a step 9 decision.

### Alignment

Every u16, u32 and u64 is assembled from bytes with shifts on read, and split into bytes with shifts on write. This fulfils the plan's "aligned reads only (`memcpy`)" with no load wider than a byte. It is also independent of host byte order, which keeps the host tests and the device byte-identical. No `reinterpret_cast` of the frame buffer, no packed structs, and `Serialization.h`'s `readPod` and `writePod` are not used.

### Documentation

Add a "GitHub sync record files (`tree.tmp`, manifest)" section to `docs/file-formats.md` with the tables above, the grammar, the torn-tail rule and version 1. That file is where CLAUDE.md asks format versions to be documented. The manifest's on-card filename is step 6's decision.

## Implementation Chunks

1. **Tree files.** CRC-32, the two stream interfaces, the in-memory stream, the writer (`begin`, `put`, `seal`) and the sequential reader for the tree kind. Tests: CRC vector, round trip, every-offset truncation of a tree file, version mismatch, bad magic, wrong kind, flipped bytes, short-write sink, writer refusals, alignment-agnostic decoding.
2. **Manifest files and by-offset reads.** The manifest grammar (sealed base and journal), `appendTo`, `drop`, `replaces`, the torn-tail rule, and `readEntryAt`. Tests: base and journal round trip, every-offset truncation of a manifest with a journal, torn-tail cases, read-back from every reported offset in both file kinds, by-offset failure cases.
3. **Compactor and documentation.** The two-pass fold, its statuses, and the `docs/file-formats.md` section. Tests: folding scenarios, duplicate and dangling targets, `ScratchFull`, the torn tail dropped, output re-read as a valid base, compaction being idempotent.

## Testing Decisions

- **What makes a good test**: only the public interface. Write with the writer into the in-memory stream, read with the reader, and assert statuses, records, offsets and flags. Corrupting a file means editing bytes of the in-memory buffer at positions computed from the documented layout. That is the format's contract, not an implementation detail.
- **Module tested**: one new gtest suite, `test/git_sync_record_format/`, registered with `add_subdirectory` in `test/CMakeLists.txt`, laid out like `test/git_sync_path_filter/CMakeLists.txt`. It compiles the record format sources (and whichever of step 1's or step 2's sources they include, if any), adds the `lib/GitSync` include directory, links `crosspoint_test_common` and `GTest::gtest_main`, and uses `gtest_discover_tests`. It needs no logging stub, since the modules do not log.
- **Prior art**: `test/git_sync_blob_hash/` and `test/git_sync_path_filter/` (suite layout, conventions), `test/streaming_json_parser/` (exhaustive every-boundary loops), `test/credential_integrity/` (integrity checks over serialized bytes).
- **Required tests**:
  - **CRC-32**: `"123456789"` gives `0xCBF43926`, the empty input gives 0, and incremental equals one-shot over uneven splits.
  - **Round trip, tree**: entries including a 1-byte path, a 255-byte path, a UTF-8 path (`Livres/Été.epub`), size 0, size `5000000000` and size `UINT64_MAX`, and an SHA with every byte value at some position, read back identically in order with `End` after. An empty tree (header plus seal with count 0) reads `End` with no records.
  - **Offsets**: every offset that `put` reports equals the offset the reader reports for that record, and `readEntryAt` on each returns the same path, SHA and size. This holds in a tree and in a manifest, base and journal. Read-backs in reverse and random order work too.
  - **Truncation at every byte offset**: for each `k` from 0 to the full length minus 1 of a tree file with several entries, the reader never returns `End` and ends with `Truncated` (or `Corrupt` when the cut leaves a complete but wrong frame). For a manifest with a base and a journal of entries and drops, the outcome for every `k` follows the reader contract above, including the exact set of journal records and `tornTail()`.
  - **Version mismatch**: version byte 0, 2 and 255 each give `WrongVersion` on the first `next` with no record returned. Bad magic gives `Corrupt`.
  - **Wrong kind**: a tree file read as a manifest, and the reverse, give `Corrupt`.
  - **Corruption**: one flipped bit in each byte of a small tree file (every position) never yields `End` with altered records: it is `Corrupt`, `Truncated` or `WrongVersion`. In a manifest, a flipped bit in the base gives `Corrupt`; in a non-final journal frame, `Corrupt`; in the final journal frame, `End` with `tornTail()` and that frame dropped.
  - **Grammar**: a seal count mismatch, a frame after a tree's seal, a drop in a tree, `replaces` in a base, a drop target at or above its own offset, an unknown frame kind and a body length of 31 (a pathless entry) all give `Corrupt`. Each case is built by hand-patching bytes with a recomputed CRC, so the CRC check is not what catches it.
  - **Writer**: refusals for an empty path, a 256-byte path, a drop in a tree, `seal` in journal mode, and `put` after a tree's seal write nothing. A sink failing on its N-th write leaves the writer sticky-failed, and the bytes written before it read as `Truncated` (tree) or as a torn or clean manifest.
  - **Compactor**:
    - a base with no journal reproduces the same live entries;
    - a journal with an add, a modify (`replaces`), a move (`replaces` to a new path), and a delete (drop), including a drop of a journal entry, yields exactly the expected live set, with its count;
    - the output re-reads as a valid manifest with no journal;
    - compacting the output again gives identical bytes;
    - a duplicate target and a dangling target give `Corrupt`;
    - a scratch array one smaller than needed gives `ScratchFull`;
    - a torn tail is dropped and reported;
    - a truncated base or a wrong version is passed through;
    - a failing sink gives `WriteFailed`.
  - **Bytes-written sanity check**, for step 6's measurement: appending N single-operation frames to a manifest of N entries grows the file by the sum of those frame sizes only, asserted against the formula 7 + 32 + pathLen per entry frame and 7 + 4 per drop.
- **Run with**: `cmake -S test -B build/test -G Ninja && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`.
- **Not tested on host**: the `HalFile` adapter and the real SD card (step 9, and the owner's step 12 checklist).

## Out of Scope

- The `HalFile` adapter, file paths and filenames on the card, and opening files for append (step 9).
- Applying the path filter, and writing `tree.tmp` from the parser (step 9; step 6's end-to-end test).
- When to compact, the remove-then-rename around it, what a `Corrupt` or `WrongVersion` manifest makes the sync do, and creating the empty manifest of a first sync (step 6). The writer can produce an empty manifest (header plus seal), and the expected handling is stated in Further Notes.
- The planner's fixed-size entries, its sorting and the file-count cap (step 5).
- Any change to `Serialization.h`, `BufferedFile.h` or the existing cache formats.
- `src/`, `platformio.ini`, UI and i18n strings.

## Further Notes

### Evidence

- Plan step 4 text and "Verified by" (`docs/plan-1.md`, § 3, Phase 1, step 4).
- Plan § 2:
  - "Streaming, not buffering" (fixed-size entries of SHA bytes plus a file offset; 64 KB budget; cap documented);
  - "Atomic writes and resume" (persist after each operation; consistent after interruption);
  - "Deletion scope".
- Plan step 5 ("respects the fixed-size memory rule").
- Plan step 6:
  - the persist after each operation;
  - the in-memory storage's remove-then-rename overwrite and torn writes;
  - the byte count at the file-count cap that catches quadratic manifest rewrites.
- Plan § 6, step 1.1 note (`GitBlobHasher::SHA_SIZE`, conventions) and step 1.2 note (`MAX_SYNC_PATH_BYTES = 255`).
- `lib/GitSync/SyncPath.h` (`MAX_SYNC_PATH_BYTES`) and `lib/GitSync/GitBlobHasher.h` (`SHA_SIZE`), at HEAD.
- `lib/hal/HalStorage.h:95` (`seek(size_t)`), `:100` (`position()`), `:101` (`int read(void*, size_t)`), `:103` (`size_t write(const uint8_t*, size_t)`). These shape the stream interfaces.
- `lib/Serialization/Serialization.h:8-25`: the `reinterpret_cast` POD I/O this format deliberately does not use.
- `src/activities/reader/ProgressFile.h:53-56`: SdFat rename does not overwrite, so the remove-then-rename pattern applies.
- CLAUDE.md: "RISC-V Alignment", "constexpr First", "Template and `std::function` Bloat", "Stack Safety" (256 bytes), "Cache File Format Versioning".

### Guidance for steps 5 and 6 (not requirements of this step)

- **Step 6 at sync start**:
  - if no manifest exists, write an empty one (header plus seal);
  - otherwise compact it into a temporary file and replace the manifest with remove then rename;
  - on `Corrupt`, `Truncated` or `WrongVersion`, stop with an error and change nothing (the owner can delete the manifest, and the next sync adopts the library);
  - on `IoError`, stop and let the owner retry.
- **During the run**: one journal frame per operation, appended after the file operation and its migration hook, with the writer resumed at the compacted file's size.
- **Step 5** keeps manifest offsets in its fixed-size entries and uses `readEntryAt` when it needs a path.

### Verification

- **Host**: the new suite passes under the `ctest` command above.
- **Firmware**: `pio run` must still succeed. Nothing in `src/` includes the new modules yet, so the image is expected to be unchanged. Format with `./bin/clang-format-fix -g`.
- **Device**: nothing to check in this step.

### Hypotheses

Step 3 (trees response parser) is being implemented while this PRD is written. These assumptions about its outcome rest on its PRD (`docs/prd/triage/phase1-step3-trees-parser.md`) and the committed tree only. This step neither includes nor calls the step 3 module, so none of them blocks implementation. The implementer of step 6 or 9 checks each against the committed step 3 code.

- **H-3-1**: Step 3's record carries `const char* path` with an explicit `size_t pathLen`, `uint8_t sha[20]` and `uint64_t size`. The writer's `put(sha, size, path, pathLen, …)` therefore takes those fields directly, and the format's u64 size field holds any value the parser emits.
- **H-3-2**: Step 3 emits paths unfiltered, up to 511 bytes. The caller applies step 2's `classifySyncPath` before `put`, so only syncable paths of at most `MAX_SYNC_PATH_BYTES` (255) bytes reach `tree.tmp`. If an unfiltered path of 256 to 511 bytes ever reached the writer, `put` refuses it, and the caller must treat that refusal as a write error, never as a silent skip.
- **H-3-3**: Step 3 reports completeness only at the end of the body (`isComplete()` and `isTruncated()`, read after the last chunk), not per record. The seal is therefore written only after the parser reports complete and not truncated, and an unsealed `tree.tmp` reads `Truncated`. That is how "abort on truncated, change nothing" (plan § 2) survives on the card.
- **H-3-4**: Step 3's record callback can stop the parser by returning `false`. The caller returns `false` when `put` fails, so a full card stops the download.
- **H-3-5**: Step 3's record has no `type` field, because only blobs are emitted. The format therefore stores no type byte, which matches the plan's `(sha[20], size, path)` for this step.
- **H-3-6**: Step 3 parses `size` exactly into `uint64_t`, including 0 and values above 4 GiB, so the format stores 8 bytes for it. Whether a size above the FAT32 limit is refused belongs to step 6.
- **H-3-7**: Step 3 makes `GitSync` include `Logging.h` and adds a host logging stub in its own suite. This step's modules do not log, so its suite needs no stub, whatever step 3 settled.
- **H-3-8**: Step 3 adds its own sibling test suite rather than a shared GitSync suite. This step then adds `test/git_sync_record_format/` the same way. If step 3 created a shared suite instead, this step's tests go there.
- **H-3-9**: Step 3's fixture uses real `git hash-object` SHAs and sizes, reused by step 6's end-to-end chain through this format. This step's tests use synthetic SHAs and do not depend on that fixture.
