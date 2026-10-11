---
title: "Sync executor for GitHub library sync"
description: "Apply the planner's operations to the SD card through a small storage interface, so that every download is verified before it lands, reading state follows moved books, the manifest is journaled after each operation, and an interrupted run resumes to the same end state as an uninterrupted one."
status: needs-triage
---

## Problem Statement

The owner presses **Update** so the SD card mirrors their private books repository (plan 1, § 1 Goal). Step 3 parses GitHub's tree listing, step 4 stores it as `tree.tmp` and keeps the sync's manifest, and step 5 turns both into an ordered list of operations: moves, adopt-or-download, downloads, deletes and prunes. Nothing yet **applies** those operations to the card.

Applying them is where the owner's books and reading state are actually at risk:

- A download cut short by a Wi-Fi drop, a power loss or Back must never leave a half-written book where the reader would open it (plan § 2, "Atomic writes and resume").
- A download whose bytes do not match the tree's size and blob SHA must never land, and must never be recorded as synced (plan step 6).
- A book renamed or moved on GitHub must keep its progress, bookmarks, recent-books entry and "open book" state, even if the run dies between the file rename and the state migration (plan § 2, "Moves and renames"; plan step 6).
- A modified book's old layout cache must be cleared, even if the run dies right after the new file lands (plan § 2; plan step 6).
- The manifest must match the card after every single operation, so the next run redoes only what is left; and the manifest's own rewrite must survive a power loss, on FAT where `rename` never overwrites (`src/activities/reader/ProgressFile.h:53-56`).
- The first sync adopts the files the owner copied from a PC by hashing them; an interrupted first sync must not hash them all again (plan § 2, "First sync and adoption"; plan step 12 checklist).
- Nothing outside the synced set may be touched: books from USB, OPDS or the web server, and everything under `/.crosspoint/` except the sync's own files and the reading state of the books it moves or rewrites (plan § 1 Goal).
- All of it within about 380 KB of RAM and a 64 KB sync heap budget that the planner already takes most of (CLAUDE.md; plan § 2, "Streaming, not buffering").

## Solution

A **sync executor** in the `GitSync` library, host-tested, that runs on any implementation of a small **storage interface**: an in-memory FAT-like double in tests, a `HalStorage` adapter on the device (step 9). Everything with a side effect outside the card is injected: the **fetch** callback (network), the **hash** callback (adoption), and two **hooks** (cache clear, recent books and app state).

- A sync is `prepare()` then `apply(planner)`. `prepare()` repairs what a crash may have left (temporary files, a manifest caught between remove and rename), creates an empty manifest on the first sync, and compacts the manifest's journal into a fresh base. `apply()` runs the step 5 planner over the compacted manifest and `tree.tmp`, and executes each operation as the planner emits it.
- Every download is streamed into one temporary file under `/.crosspoint/gitsync/`, hashed with the step 1 blob hasher as it is written, verified against the tree's size and SHA, then renamed onto its target. A mismatch deletes the temporary file and reports a per-file error; nothing lands, nothing is recorded.
- After each applied operation, exactly one journal frame is appended to the manifest (step 4's journal), and flushed. That frame is always the **last** step of an operation: file rename, reading-state migration, cache clear and directory prune all happen before it. A run that dies anywhere before the frame leaves the manifest saying "not done", and every step of the operation is idempotent, so the next run redoes the whole operation and ends in the same state.
- A move renames the file, then migrates the reading state: the `epub_`, `txt_` and `xtc_` cache directories and the bookmark file are renamed through the storage interface, and recent books and app state are updated through one hook. The cache-directory names come from one shared function that the reader's own code is changed to call, so the executor and the reader can never disagree, on the host or on the device.
- Back sets a cancel flag that is honoured between operations.
- The executor's own RAM is one fixed object of at most 3 KiB; compaction borrows 8.6 KB transiently and frees it before the planner is allocated.

## User Stories

1. As the library owner, I want a new book on GitHub to appear on the card only once it is fully downloaded and verified, so that I never open a half-written book.
2. As the library owner, I want a download whose bytes do not match GitHub's SHA or size to be discarded and reported, so that a corrupted transfer never replaces a good book.
3. As the library owner, I want a download that failed or was cut short to be fetched again on the next Update, so that one bad transfer does not leave a book missing forever.
4. As the library owner, I want one failed file not to stop the rest of the sync, so that a single network hiccup does not cost me the whole Update.
5. As the library owner, I want a book modified on GitHub to replace the old file on the card, so that I read the new version.
6. As the library owner, I want a modified book's layout cache cleared, so that the reader does not show the old version's pages.
7. As the library owner, I want the cache of a path that receives new content cleared even when the path was not in the manifest, so that a stale cache from a book deleted earlier never attaches to a new book at that path.
8. As the library owner, I want an empty file in my repository to be created on the card without a network request, so that it syncs even though the downloader treats an empty body as an error.
9. As the library owner, I want folders that the new books live in to be created as needed, so that nested repository folders mirror on the card.
10. As the library owner, I want a book renamed or moved on GitHub to be renamed on the card with no download, so that the sync is fast.
11. As the library owner, I want my progress and layout cache to follow a moved book, so that I resume where I was.
12. As the library owner, I want my bookmarks to follow a moved book, so that I do not lose them.
13. As the library owner, I want the recent-books list and the currently open book to follow a moved book, so that the home screen still opens it.
14. As the library owner, I want a sync interrupted right after a move's file rename to finish migrating that book's reading state on the next Update, so that my progress is never orphaned.
15. As the library owner, I want a sync interrupted right after a modified book lands to still clear that book's cache on the next Update, so that I never read the new file through the old cache.
16. As the library owner, I want the files I copied from my PC before the first sync to be hashed and adopted, not downloaded, so that the first sync takes minutes, not hours.
17. As the library owner, I want an adopted file to keep its reading state, so that books I read before setting up sync keep their progress.
18. As the library owner, I want a local file whose content differs from GitHub's to be overwritten, so that GitHub always wins.
19. As the library owner, I want an interrupted first sync to resume without hashing again the files it already adopted, so that pressing Back during adoption costs nothing.
20. As the library owner, I want a book deleted on GitHub to be deleted on the card, and a folder emptied by it to disappear, so that the card mirrors the repository.
21. As the library owner, I want a folder that still holds my own files to stay, so that pruning never removes anything I put there.
22. As the library owner, I want books I added by USB, OPDS or the web server never to be touched, so that the sync is safe to use alongside them.
23. As the library owner, I want nothing under `/.crosspoint/` to be touched except the sync's own files and the reading state of books it moves or rewrites, so that settings, credentials and other books' progress are safe.
24. As the library owner, I want Back to stop the sync between two files, so that I can cancel without corrupting anything.
25. As the library owner, I want a power loss at any moment to leave the card and the manifest consistent, and the next Update to finish the job, so that I never have to repair the card from a PC.
26. As the library owner, I want the manifest rewrite at the start of a sync to survive a power loss between its remove and its rename, so that I never lose the manifest and fall back to a full first sync.
27. As the library owner, I want a damaged or wrong-version manifest to stop the sync before anything changes, so that a broken list never deletes books.
28. As the library owner, I want the summary to show how many files were added, updated, moved, deleted, adopted, skipped and failed, so that I know what the Update did.
29. As the library owner, I want the manifest's SD writes to stay proportional to what changed, so that a large first sync does not wear the card or take quadratic time.
30. As a firmware developer (step 9), I want a storage interface shaped like `HalStorage` (open, read, write, seek, exists, mkdir, remove, rmdir, rename), so that the device adapter only forwards calls.
31. As a firmware developer (step 9), I want the storage contract written down (rename never overwrites, lookup ignores case, FAT-forbidden names are rejected, an open file cannot be renamed or removed), so that I can review the `HalStorage` adapter against it.
32. As a firmware developer (step 9), I want the fetch to be one function-pointer callback that pushes chunks into a sink, so that I can map it onto `HttpDownloader::fetchUrl`'s data callback without a `std::function` in library code.
33. As a firmware developer (step 9), I want the adoption hash to be one function-pointer callback, so that the device reads the local file through `HalStorage` with its own 4 KB buffer.
34. As a firmware developer (step 9), I want the cache-clear hook and the move hook to receive absolute SD paths, so that I call `clearBookCache`, `RECENT_BOOKS.updatePath` and `APP_STATE.openEpubPath` exactly as `EpubReaderActivity.cpp:127-146` does.
35. As a firmware developer (step 9), I want a progress callback before each operation with its kind and path, so that the progress screen shows `n/total : file` and the activity can pump Back.
36. As a firmware developer (step 9), I want the sync's SD paths (`/.crosspoint/gitsync/` and the files in it) as public constants, so that the glue and the executor never disagree.
37. As a firmware developer (step 9), I want the executor's RAM in one object of documented size and the compaction's transient buffer freed before the planner exists, so that I can budget the run against 64 KB.
38. As a firmware developer (step 10), I want a run result with a status and per-kind counts, so that the summary or error screen needs no further logic.
39. As a reader maintainer, I want the cache-directory name derived by one function that every reader site calls, so that a change to it reaches the sync automatically.
40. As a test author, I want an in-memory storage that behaves like FAT and SdFat and can fail at its N-th mutating call with a torn write, so that every crash point is tested on the host.
41. As a test author, I want to feed hand-made operations to the executor, so that its behaviour is tested independently of the planner's choices.
42. As a test author, I want the expected reading-state paths to come from the reader's own functions, linked into the test, so that the test cannot agree with a wrong executor by repeating its mistake.
43. As a maintainer, I want the bytes written by a first sync at the file-count cap recorded in the step notes next to the RAM cap, so that a regression to quadratic manifest writes is visible.

## Implementation Decisions

### Modules

All new library code follows the conventions of steps 1 to 4: no namespace, `#pragma once`, `static constexpr` constants, no Arduino include, no `std::function`, no `std::string` and no heap inside the library except the one documented transient buffer, `makeUniqueNoThrow` for it, `LOG_ERR` before every error return (host tests use the no-op logging stub of the tree-parser suite).

1. **Book cache path** (new, header-only, outside `GitSync`, shared with the reader): the single derivation of a book's cache directory.
   - `bookCacheKey(std::string_view absPath)` returns `std::hash<std::string_view>{}(absPath)`.
   - A writer into a caller buffer, `formatBookCacheDir(out, capacity, cacheRoot, kind, absPath)`, with `kind` one of `Epub`, `Txt`, `Xtc` mapped by a `constexpr` table to `epub_`, `txt_`, `xtc_`, producing `<cacheRoot>/<prefix><decimal key>`, the decimal written exactly as `std::to_string` writes it.
   - A `std::string` convenience overload for the reader's call sites.
   - The reader is changed to call it, with byte-identical results: the `Epub` and `Xtc` constructors, the `Txt` constructor, and `moveFinishedBookToReadFolder`'s new cache path. Nothing else in the reader changes.
   - Why shared rather than copied: the reader's derivation lives in classes the host tests cannot link (they pull in `HalStorage`, the ZIP and image stacks), and `std::hash<std::string>` gives different values on the host and on the device (see Further Notes, "Hash width"). With one function, the executor and the reader compute the same name on whichever platform they run, by construction.
   - Why `std::string_view`: the standard requires `hash<string_view>` and `hash<string>` to agree for the same characters (libstdc++ routes both to `_Hash_bytes` with the same seed), and the view avoids a heap `std::string` in the executor.
2. **Bookmark path** (in `GitSync`): `formatBookmarkPath(out, capacity, absPath)` reproduces `BookmarkUtil::getBookmarkPath` (drop the leading `/`, replace `/` and `\` by `_`, cut at the last `.` of the flattened name, append `.json`, prefix `/.crosspoint/bookmarks/`) into a caller buffer. It is a copy because a `lib/` module cannot include `src/util/`; the test links the real `BookmarkUtil.cpp`, which is host-linkable (it depends only on the standard library; checked with g++ 13.3, `-Wall -Wextra -pedantic`), and compares the two over a table, so any drift fails the build.
3. **Storage interface** (in `GitSync`): `SyncStorage` and `SyncFile`, abstract classes (virtual calls, like step 4's `RecordSource`).
4. **Sync executor** (in `GitSync`): `SyncExecutor`, its callbacks, hooks, operation outcome and run result.
5. **In-memory storage** (test double, in the test suite, host-only, may use `std::map` and `std::string`): `MemorySyncStorage`.

### Storage interface contract

Paths are absolute (`/Livres/a.epub`), `'\0'`-terminated UTF-8. The device adapter (step 9) forwards to `HalStorage`; the contract below is what step 9's review checks.

- `SyncFile` derives from step 4's `RecordSource` (`read`, `seek`) and `RecordSink` (`write`), so it can be handed to a `RecordReader`, `RecordWriter`, `ManifestCompactor` or the planner directly. It adds `flush()` (returns false on failure) and `size()`.
- `open(path, mode)` returns a storage-owned `SyncFile*` or `nullptr`; `close(file)` releases it. At most `MAX_OPEN_FILES = 3` files are open at once (manifest, tree, download temp); the adapter holds that many `HalFile` slots, so no heap per open. The executor wraps each open in a small guard that closes on every path.
  - `Read`: an existing file, read-only.
  - `Truncate`: creates or truncates, write-only. The parent directory must exist.
  - `Append`: an existing file, read anywhere through `seek`, every write lands at the end (SdFat `O_RDWR | O_APPEND`). Used for the manifest, so the planner and the executor can read base records by offset on the same handle the journal is appended to.
- `exists(path)`, `fileSize(path, out)` (false when missing or a directory), `isEmptyDirectory(path)` (false when missing, not a directory, or holding anything).
- `mkdirs(path)` creates the directory and its missing parents; true when it already exists as a directory.
- `remove(path)` removes a file, never a directory. `rmdir(path)` removes an empty directory only.
- `rename(from, to)` renames a file or a directory. It **fails when `to` exists**, under any letter case, like SdFat (`ProgressFile.h:53-56`). Every overwrite is therefore a remove then a rename.
- Lookup ignores letter case the way FAT long names do. A name holding a FAT-forbidden character (`: * ? " < > | \`) is rejected by every call that would create it.
- A file that is open cannot be renamed or removed (SdFat must not rename a path with an open handle, `ProgressFile.h:50`).
- Mutating calls are `open` with `Truncate`, `write`, `rename`, `remove`, `mkdirs`, `rmdir`. Reads, `exists`, `fileSize`, `isEmptyDirectory`, `flush` and `close` are not.

### Injected callbacks and hooks

All are a plain function pointer plus a `void*` context (CLAUDE.md, "Template and `std::function` Bloat"), gathered in one `SyncExecutorCallbacks` struct passed at construction.

- **Fetch**: `bool (*fetch)(void* ctx, const SyncFetchRequest& request, const SyncChunkSink& sink)`.
  - `SyncFetchRequest { const uint8_t* sha; uint64_t size; const char* repoPath; }`: the glue builds the blob URL from the SHA (content-addressed, plan § 2 "Tree snapshot"); the path is for logs and progress.
  - `SyncChunkSink { void* ctx; bool (*write)(void* ctx, const uint8_t* data, size_t len); }`: the executor's sink hashes the chunk, writes it to the temp file, and returns false when the write fails or the bytes exceed the declared size; the fetch then stops and returns false.
  - The fetch returns true only when the body ended normally. It maps onto `HttpDownloader::fetchUrl`'s data callback in step 9.
  - It is never called for a size-0 entry (`HttpDownloader.cpp:309-313` treats an empty body as an error).
- **Hash**: `bool (*hashFile)(void* ctx, const char* absPath, uint64_t expectedSize, uint8_t outSha[20])`. The glue reads the file through `HalStorage` in 4 KB chunks into a step 1 `GitBlobHasher` (plan step 9). False means "unknown": the executor then downloads, which is safe because GitHub wins.
- **Cache-clear hook**: `bool (*clearBookCache)(void* ctx, const char* absPath)`. Device: `clearBookCache(path)` (`src/util/BookCacheUtils.cpp:23`), returning true. Must be idempotent.
- **Move hook**: `bool (*bookMoved)(void* ctx, const char* oldAbsPath, const char* newAbsPath)`. Device: `RECENT_BOOKS.updatePath` with the cache paths the glue derives with the shared book-cache-path function and the reader's extension rules, then `APP_STATE.openEpubPath` and `saveToFile`, as in `EpubReaderActivity.cpp:127-146`. Must be idempotent: `updatePath` is a no-op when no entry has the old path, and the open path is compared before it is changed. The executor does not pass cache paths, so it needs no extension rules.
- **Progress**: `void (*onProgress)(void* ctx, const SyncProgress& progress)`, called before each operation with `{done, total, kind, repoPath}`, so the progress screen shows `n/total : file` (plan § 2, UI). `total` is the count the planner's `SyncOpSink::onTotal` delivers exactly once before the first operation, 0 included (H-5-12); the executor stores it and reports it unchanged with every progress call. `done` counts operations started, from 1 to `total`. The device glue pumps input there, so Back can call `requestCancel()`.
- A hook returning false is a **fatal** storage error (below): on the device it means an SD write failed, and the journal frame must not be written after a half-done migration.

### On-card paths

Public `constexpr` constants, so step 9 and the tests use the same names:

- work directory `/.crosspoint/gitsync`;
- manifest `/.crosspoint/gitsync/manifest.bin` (step 4 format, kind manifest; added to `docs/file-formats.md`);
- manifest rewrite `/.crosspoint/gitsync/manifest.new`;
- download temp `/.crosspoint/gitsync/download.tmp`;
- tree `/.crosspoint/gitsync/tree.tmp` (written by step 9 before `apply()`).

`manifest.new` deliberately does not end in `.tmp`: step 9's "delete leftover temporary files" must not delete a manifest rewrite that `prepare()` may need to promote. Step 9 deletes only `*.tmp` files other than those the executor names, or nothing; the executor cleans its own.

### Lifecycle

`SyncExecutor(SyncStorage&, const SyncExecutorCallbacks&)`, held through `makeUniqueNoThrow`, then:

1. **`prepare()`**, before the tree is fetched and before the planner is allocated:
   1. `mkdirs` the work directory.
   2. Remove `download.tmp` if it exists.
   3. Manifest recovery:
      - manifest present: remove a leftover `manifest.new`;
      - manifest missing and `manifest.new` reads to `End` as a sealed manifest with no journal: rename it into place (a crash hit the window between remove and rename);
      - otherwise (first sync, or garbage): remove `manifest.new` if present, write an empty manifest (header plus seal) to `manifest.new`, flush, close, rename into place.
   4. Read the manifest to `End`. `Truncated`, `Corrupt`, `WrongVersion` → `ManifestError` with that status, nothing changed; `IoError` → `StorageError`.
   5. When the journal holds at least one frame, or `tornTail()` is set: compact with step 4's `ManifestCompactor` into `manifest.new`, flush, close, remove the manifest, rename `manifest.new` into place. The compactor and a scratch of `MAX_SYNC_FILES` `uint32_t` (8,000 B, H-5-9) come from `makeUniqueNoThrow` and are freed before `prepare()` returns. `ScratchFull` or `Corrupt` → `ManifestError`; a failing write → `StorageError`.
   6. A manifest with an empty journal is not rewritten, so a no-change Update writes nothing.
   7. Record the manifest's `endOffset()` for the journal writer.
   - Why compaction bounds the scratch: within one run every journal target names a distinct base entry, and the planner refuses a manifest above the cap before any operation, so a journal never holds more than `MAX_SYNC_FILES` targets.
2. The glue streams the tree into `tree.tmp` (step 9), then allocates the planner.
3. **`apply(SyncPlanner&)`**: opens the manifest (`Append`) and the tree (`Read`), seeks both to offset 0 (the planner expects its sources at their first byte), starts a `RecordWriter` with `appendTo(endOffset)` on the manifest handle, and calls `planner.plan(manifest, tree, localExists, this, sink)`:
   - `localExists` is a `SyncLocalExistsFn` answering `storage.exists("/" + path)`;
   - `sink` is a `SyncOpSink { this, onOp, onTotal }`: `onTotal` stores the total for progress, `onOp` checks the cancel flag, calls `onProgress`, then `execute(op)`, and returns false on cancel or on a fatal outcome;
   - then it closes both files and maps the `SyncPlanResult` into the run result (H-5-11).
4. **`begin()` / `execute(const SyncOp&)` / `end()`** are public too: `apply()` is `begin`, plan, `end`. Tests use them to feed hand-made operations; step 9 can use them if it ever writes operations to a file.

### Execution of each operation

Paths are read back by offset with two executor-owned `RecordReader`s (manifest, tree) on the open handles, and copied as absolute paths into two fixed buffers. Every path an operation names is checked with `classifySyncPath` before anything is touched; a path that is not `Syncable` is refused with a per-file error and never touched (defence in depth: the planner already ignores them, H-5-7).

**Download** (`manifestOffset` set: a modify; `NO_OFFSET`: new). Target T = the tree path.

1. Open `download.tmp` with `Truncate`; `GitBlobHasher::begin(size)`.
2. Size 0: write nothing. Otherwise call the fetch with the executor's sink.
3. Flush and close the temp file. Verify: fetch returned true, `finish()` true (size), and the SHA equals the tree entry's. On any mismatch: remove the temp file, per-file error (`FetchFailed` or `VerifyFailed`), manifest unchanged.
4. `mkdirs` T's parent. Failure → remove the temp, per-file error `TargetUnwritable` (a file where a folder should be, or a name the card refuses).
5. Clear the reading cache through the hook: for a modify, the replaced manifest path; and T when it differs byte-wise from it, or for a new download (user story 7, web-server parity: `clearBookCache` on upload).
6. If T exists: remove it (a file) or fail with `TargetUnwritable` (a directory).
7. Rename `download.tmp` onto T. Failure → `TargetUnwritable`.
8. Append the journal entry `(sha, size, T, replaces = manifestOffset or NO_OFFSET)` and flush.

**AdoptOrDownload**. Target T = the tree path.

1. `fileSize(T)` equals the tree size and the hash callback returns true with the tree SHA → append the journal entry `(sha, size, T, NO_OFFSET)`, flush. No cache clear: an adopted file keeps its reading state (user story 17).
2. Otherwise → the Download sequence above with no replaced entry (overwrite, GitHub wins). A size mismatch skips the hash entirely.

**Move**. Source S = the manifest path, destination D = the tree path.

1. `sourceExists && !targetExists` (H-5-5): `mkdirs` D's parent, rename S → D. Failure → per-file error `TargetUnwritable`, nothing migrated.
2. `!sourceExists && targetExists` (an interrupted move): verify D as in AdoptOrDownload step 1. Match → continue with migration. Mismatch → download D (overwrite), then continue with migration: the content is the same book.
3. **Migration**, each step idempotent, all before the journal frame:
   1. For each kind (`Epub`, `Txt`, `Xtc`): old = cache dir of S, new = cache dir of D, from the shared function.
      - When new exists while old exists: call the cache-clear hook on D once (a stale cache of an earlier book at D), then re-check.
      - old exists and new does not → rename old → new. A failure is fatal.
      - Still both present (a kind the hook does not clear) → leave both, count `stateConflicts`, log.
   2. Bookmark: old = bookmark path of S, new = bookmark path of D. Equal (the flattened names collide, e.g. `a/b.epub` and `a_b.epub`) → nothing. old exists and new does not → rename. Both exist → leave, count `stateConflicts`.
   3. Move hook `(S, D)`.
4. Prune S's ancestors (below).
5. Append the journal entry `(sha, size, D, replaces = manifestOffset)`, flush.

**Delete**. P = the manifest path. If P exists as a file, remove it; if it is a directory, per-file error `TargetUnwritable`, manifest unchanged. Prune P's ancestors. Append a drop frame for `manifestOffset`, flush. Reading state of a deleted book is left in place (the plan asks for no cleanup; it also lets a book that comes back find its progress).

**Prune** (an operation from the planner, and inline in Delete and Move): walk P's ancestors from the deepest, never the root and never a path under `/.crosspoint/`. Missing → continue upward (a crash may have removed it already). `isEmptyDirectory` → `rmdir`, failure fatal. Not empty → stop. Doing it inline, before the frame, is what makes an interrupted run end with the same directories: a planner `Prune` comes after its `Delete` was journaled and would never be emitted again. The planner's `Prune` operations then find nothing left to do and stay as a harmless second attempt.

**Skip** (H-5-7): no storage call, counted per reason, logged at info level, never an error.

### Failure classes

- **Per-file error** (the operation is abandoned, the manifest unchanged, the run continues): fetch failure, verification failure, refused path, `TargetUnwritable`. The next run retries it, because the manifest still says it is not done.
- **Fatal** (the run stops with `StorageError`; nothing after it runs): any failure on the work directory (temp open, temp write, temp remove, temp flush), the journal append or its flush, a reading-state rename, `rmdir` of an empty directory, a hook returning false, a reader `IoError`. These mean the card is failing; continuing would multiply errors, and stopping leaves a state the next run resumes from.
- **Cancel**: `requestCancel()` sets a `volatile bool` (a single-writer flag polled between operations, per CLAUDE.md's ISR/task table); the sink checks it before each operation and returns false, so the planner stops with `Aborted` and the run reports `Cancelled`. An operation in progress always completes or fails as a whole.

### Resume model

The rule is: **the journal frame is the commit point, and it is written last.** Every step before it is idempotent on rerun:

| Interrupted after | Manifest says | Next run's plan (H-5-5, H-5-6) | Result |
|---|---|---|---|
| temp written, before target rename | old | same Download | temp removed by `prepare()`, re-fetched |
| modify: cache cleared, target removed | old entry | same modify Download | re-fetched, cache cleared again (no-op), lands |
| modify or new: target renamed, before frame | old entry / absent | modify Download / AdoptOrDownload | re-fetched (modify) or adopted by hash (new); cache clear repeated |
| move: file renamed, before migration | S | Move with `!sourceExists && targetExists` | D verified by hash, migration done, frame |
| move: some reading state migrated | S | same | each remaining step done, finished ones skipped |
| adopt: hashed, before frame | absent | AdoptOrDownload | that one file hashed again; every journaled adoption is in the manifest and never re-hashed |
| delete: file removed or dirs pruned | P | Delete | missing file and missing dirs skipped, frame |
| compaction: `manifest.new` written | old + journal | — | `prepare()` removes `manifest.new`, compacts again |
| compaction: manifest removed | missing | — | `prepare()` promotes `manifest.new` |
| journal frame torn | old | as if not applied | `prepare()` compacts the torn tail away (step 4 `tornTail()`), the operation is redone |

Known cost, accepted: a modify interrupted after its rename is downloaded once more on the next run (hashing every modified target first would add a full SD read to every modify to save one download after a rare crash).

### Run result

`SyncRunResult` carries:

- `status`: `Ok`, `Cancelled`, `StorageError`, `ManifestError`, `TreeError`, `CapExceeded`, `OutOfMemory` (the compaction scratch), `NotPrepared`;
- the step 4 `RecordStatus` behind a manifest or tree error, and the planner's counts for a cap refusal (H-5-11);
- counts: `added`, `updated`, `moved`, `deleted`, `adopted`, `skippedCaseCollision`, `skippedUnsafeName`, `fileErrors`, `stateConflicts`, `pruned`.

`Ok` with `fileErrors > 0` is a success with failures to retry; step 10 shows both.

### Memory

| Part | Size |
|---|---|
| Two `RecordReader`s (manifest and tree, by-offset reads) | ~640 B |
| One `RecordWriter` (journal) | ~310 B |
| One `GitBlobHasher` | ~120 B |
| Two path buffers (absolute repository paths, 1 + 255 + 1) | 514 B |
| Two derived-path buffers (bookmark path up to 23 + 255 + 5 + 1 = 284; cache dirs are shorter) | 576 B |
| Counters, flags, callbacks, recorded offsets | < 200 B |
| **`SyncExecutor`** | **≈ 2.4 KB, `static_assert` ≤ 3 KiB (3,072 B)** |
| Transient in `prepare()`: compactor + scratch, freed before return | 640 + 8,000 B |

- Allocation: the executor through `makeUniqueNoThrow`, checked, `LOG_ERR` on failure. Static storage was rejected (it would hold DRAM for a feature used occasionally); the stack is out of the question (CLAUDE.md, 256-byte locals).
- No heap per operation: no `std::string`, no `std::vector`; the scratch is the only allocation and happens once per sync.
- Peak during `apply()`: planner ≤ 50 KiB (H-5-10) + executor ≤ 3 KiB, leaving about 11 KB of the 64 KB for the glue's hash buffer, the downloader's chunk buffer and the storage adapter's three `HalFile` slots (TLS excluded, plan § 2). Peak during `prepare()`: about 11.4 KB, with no planner alive.

### Manifest write cost

Persisting one operation appends one frame: 39 bytes plus the path for an entry, 11 for a drop (step 1.4 notes). A run never rewrites the manifest except the one compaction in `prepare()`, so bytes written per run are linear in the number of operations plus one base rewrite. The cap test pins it (Testing Decisions) and the implementer records the measured totals in the step notes next to the RAM cap.

## Implementation Chunks

1. **Book cache path and bookmark path.** The shared cache-dir function, the reader's four call sites switched to it, and `formatBookmarkPath`. Tests: the bookmark table against the linked `BookmarkUtil::getBookmarkPath` (`/Livres/SF/Dune.epub`, `/a.b/c`, `/README.md`, `/x`, a name with a space, a UTF-8 name, a path with `\`, a 255-byte path); a refactor guard that the shared function equals the legacy expression `root + "/epub_" + std::to_string(std::hash<std::string>{}(path))` for each prefix; `hash<string_view>` equals `hash<string>`; buffer too small is refused. `pio run` must stay green, since reader code changes.
2. **Storage interface and in-memory storage.** `SyncStorage`, `SyncFile`, the path constants, and `MemorySyncStorage` with its own tests: rename refuses an existing destination including a case variant; lookup ignores case (using step 5's `syncPathsEqualFolded`, H-5-13); FAT-forbidden names rejected; an open file cannot be renamed or removed; `rmdir` refuses a non-empty directory; `Append` writes land at the end; the N-th mutating call fails with a torn write and every later mutating call fails; bytes written are counted.
3. **Executor core with hand-made operations.** `prepare()` (recovery, temp cleanup, empty manifest, compaction), `begin`/`execute`/`end`, the journal append, Download (new, modify, size 0, verification, overwrite, cache-clear hook), AdoptOrDownload, Delete with inline prune, Prune, Skip, refused paths, failure classes, cancel, progress, result counts.
4. **Moves and reading-state migration.** Move in both existence cases, the cache-dir and bookmark renames, the stale-destination clear, conflicts, the move hook, and the resume of an interrupted move and an interrupted modify, still with hand-made operations.
5. **Planner wiring and the plan's system tests** (the planner core is committed; the move, swap and skip parts of the scenarios need step 5's chunk 3, H-5-5 to H-5-8). `apply()`, then: the fault sweep at every N over a mixed plan and an adopt-only plan, the cap test with bytes written, the safety test, and the end-to-end chain from `TreeFixture.h`.

## Testing Decisions

- **What makes a good test**: only the public interface (`prepare`, `apply`, `begin`/`execute`/`end`, the result) and observable state: the in-memory card (files, contents, directories), the manifest read back through step 4's reader, the hook and fetch fakes' records. No test inspects executor internals.
- **Suite**: a new `test/git_sync_executor/`, registered with `add_subdirectory` in `test/CMakeLists.txt`, laid out like `test/git_sync_record_format/`. It compiles the executor, step 1 to 4 sources, step 5's `SyncPlanner.cpp` and `SyncPathFold.cpp` (as the committed `test/git_sync_planner/` suite does), `src/util/BookmarkUtil.cpp`, the shared book-cache-path header's directory, and the tree-parser suite's `stubs/Logging.h` (or a copy), with `crosspoint_test_common` and `GTest::gtest_main`. Chunk 1's tests may live in the same suite.
- **Expected migration paths come from the reader's own code, never from the test author**:
  - bookmark paths: `BookmarkUtil::getBookmarkPath(absPath)`, linked from `src/util/BookmarkUtil.cpp`;
  - cache directories: the shared book-cache-path function, which is the reader's derivation after chunk 1 (Epub, Txt, Xtc and `moveFinishedBookToReadFolder` call it);
  - no hash value is ever written as a literal in a test: host and device values differ (Further Notes, "Hash width"), and the literal would only pin the host.
- **In-memory storage**: a tree of directories and files with case-preserving names and case-insensitive lookup; the FAT and SdFat rules of the contract above; a fault plan `failAt(N, tear)` where the N-th mutating call fails, a failing `write` keeps a prefix of its bytes (`tear = Half` keeps `len / 2`, `tear = None` keeps 0), and every later mutating call fails while reads keep working; counters for mutating calls and bytes written; a snapshot of files, contents and directories for equality.
- **Fakes**: a fetch serving `TREE_FIXTURE_BLOBS`-style content by SHA, with modes for wrong bytes, short body returning true, failure mid-stream, and a per-SHA call count; a hash callback reading the in-memory storage into a `GitBlobHasher`, recording each path hashed; a cache-clear hook that records the path and removes the three derived cache dirs through the storage (so its calls are mutating calls in the sweep, like `clearBookCache` on the device); a move hook keeping a fake recent-books list and open-book path, idempotent like the device's.
- **The plan's "Verified by", one test each or more**:
  - **Fault sweep**: for a mixed scenario holding every operation kind (a move with epub cache, txt cache and bookmark present, a modify with a cache, a new nested download, a size-0 file, an adopt that matches, an adopt that does not, a delete that empties its folder, a delete in a folder that keeps a user file, a case-collision skip and an unsafe-name skip), run once uninterrupted and count its mutating calls M; then for every N in 1..M and for both tear policies, start from the same initial card, run `prepare` + `apply` with `failAt(N)`, expect a non-`Ok` status, clear the fault, run again with fresh executor and planner, and expect `Ok` with the same files, contents, directories, manifest live entries (read through compaction or the reader), recent-books fake and open-book fake as the uninterrupted run. The same sweep over an adopt-only first sync also asserts that the rerun's hash calls never include a path whose adoption the interrupted run had journaled.
  - **Cap and bytes written**: a first sync of `MAX_SYNC_FILES` (2,000, H-5-9) files all present locally with matching content; assert `Ok`, 2,000 adoptions, zero fetches, and bytes written ≤ empty manifest + Σ entry frame sizes + 1 KiB; a second, no-change run writes ≤ one compacted base + 1 KiB. Both totals are recorded in the step notes next to the RAM cap.
  - **Never touched**: a card seeded with a user book at the root, a user book inside a synced folder, `/.crosspoint/settings.json`, another book's cache dir and bookmark, and a remote tree with `.crosspoint/evil.bin` and `.github/x`; after a sync that adds, modifies, moves and deletes, every seeded item is byte-identical, and the only paths under `/.crosspoint/` that changed are the work directory and the moved or rewritten books' derived reading-state paths.
  - **Bad bytes**: a fetch returning wrong bytes, a short body, and too many bytes each leave no file at the target, no temp file after the next `prepare()`, and no manifest entry; a rerun calls the fetch again for that SHA and lands it.
  - **Failed download**: a fetch failing mid-stream leaves no partial file at the target.
  - **Interrupted move and modify**: a fault right after a move's file rename (located from the uninterrupted run's call log) leaves D in place and the reading state at S; the rerun moves the cache dirs and the bookmark to D's derived paths and calls the move hook. The same for a modify: a fault right after its target rename; the rerun clears the cache.
  - **Migration paths**: for a move of `Livres/SF/Dune.epub` to `Lus/Dune.epub` and one of a `.txt`, the renamed paths equal the reader's derivations of the absolute paths; a bookmark-name collision (`a/b.epub` → `a_b.epub`) does nothing; a stale destination cache is cleared through the hook before the rename.
  - **End-to-end chain**: `TREE_FIXTURE_BODY` fed to `GitTreeParser`, its records written with `RecordWriter` into `tree.tmp` on the in-memory storage, then `prepare` + `apply` with a real planner, on an empty card: the card holds exactly the five non-hidden blobs with their contents (including `Livres/vide.txt`, size 0, created with no fetch), nothing under `.atelier/`, and the manifest's live entries are those five with their SHAs and sizes. A second `apply` with the same tree performs no operation and writes nothing.
- **Additional pinned behaviour**: per-file errors do not stop the run; a fatal error does; cancel before the K-th operation stops after exactly K - 1 operations with `Cancelled` and a consistent manifest; progress is called once per operation in order, each call carrying the `total` from `onTotal` and `done` from 1 to `total` (an empty plan calls no progress, and the total 0 is still recorded); a corrupt or wrong-version manifest gives `ManifestError` with zero mutating calls; a `manifest.new` left in each state is handled as the recovery rules say; `sizeof(SyncExecutor) <= 3072` is a `static_assert`.
- **Prior art**: `test/git_sync_record_format/` (writer into memory, fault-injecting sources, every-byte loops), `test/git_sync_tree_parser/` (fixture, logging stub), `test/opds_filename/` (linking `src/util/` sources into a host test).
- **Run with**: `cmake -S test -B build/test -G Ninja && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`; `pio run`; `./bin/clang-format-fix -g`.
- **Not tested here**: the `HalStorage` adapter, the HTTP fetch, the device hooks, wall-clock duration and heap on the device (steps 9, 10, 12).

## Out of Scope

- The `HalStorage` adapter, the fetch over `HttpDownloader`, the hash callback's device implementation, the device hooks, streaming the tree into `tree.tmp`, the `truncated` abort, the progress screen and Back wiring (steps 8 to 10).
- User-facing strings (step 10).
- Clearing reading state of deleted books, and migrating state for a case-only rename (the planner emits nothing for it).
- Copy detection, and moving a book that is open in the reader during a sync (the sync runs from its own activity).
- FAT's silent stripping of trailing dots and spaces in names; such a remote name lands under the stripped name and is retried on later runs as a per-file error at worst.
- Any change to steps 1 to 5's modules, beyond consuming them.
- Any change to `BookmarkUtil`.

## Further Notes

### Evidence

- Plan step 6 text and "Verified by" (`docs/plan-1.md`, § 3, Phase 1, step 6); plan § 2 "Moves and renames", "First sync and adoption", "Atomic writes and resume", "Tree snapshot", "Streaming, not buffering"; plan steps 9 and 12 for the device side and the owner checklist.
- Step notes 1.1 (`finish()` fails on a size mismatch, giving the size check), 1.2 (`classifySyncPath`, `MAX_SYNC_PATH_BYTES = 255`, FAT-forbidden characters left to steps 5/6), 1.3 (`GitTreeParser`, `TreeFixture.h` with a size-0 blob), 1.4 (journal frames of 39 + path and 11 bytes, `tornTail()`, `endOffset()`, `ManifestCompactor` run by step 6 at sync start).
- `lib/GitSync/RecordReader.h` (`readEntryAt`, `tornTail`, `endOffset`), `RecordWriter.h` (`appendTo`, `put`, `drop`, one sink write per frame), `ManifestCompactor.h` (scratch, statuses), `RecordStream.h`, `RecordFormat.h` (`NO_OFFSET`, `EMPTY_MANIFEST_SIZE`), at HEAD.
- Reader derivations: `lib/Epub/Epub.h:43`, `lib/Xtc/Xtc.h:32`, `lib/Txt/Txt.cpp:9-11`, `src/activities/reader/EpubReaderActivity.cpp:135`; `src/util/BookmarkUtil.cpp:8-19`; `src/util/BookCacheUtils.cpp:9-34`; `src/RecentBooksStore.h:45-49` and `.cpp:92-104` (`updatePath` is a no-op for an unknown path and rewrites `coverBmpPath` under the cache dir); `src/activities/reader/ProgressFile.h:50,53-56` (rename does not overwrite, no rename of an open file).
- `src/network/HttpDownloader.h:17,38` (`fetchUrl` with a data callback returning bool), `HttpDownloader.cpp:309-313` (empty body is an error).
- `lib/hal/HalStorage.h:48-53` (`open` with flags, `mkdir`, `exists`, `remove`, `rename`, `rmdir`).
- CLAUDE.md: "Resource Protocol" (stack, heap, `makeUniqueNoThrow`, no `std::string` in hot paths), "SD Persistence Throttling", "DESTRUCTOR_CLOSES_FILE", "ISR vs Task Shared State" (`volatile bool` single-writer flag), "Template and `std::function` Bloat", "Error Handling Philosophy".

### Hash width

Checked on 2026-10-11 in this repository's environment:

- Host tests: g++ 13.3 on x86-64, `sizeof(size_t) == 8`; `std::hash<std::string>{}("/Livres/a.epub")` is a 20-digit value (15571150664331224614).
- Device: PlatformIO's `riscv32-esp-elf-g++` 14.2.0 defines `__SIZEOF_SIZE_T__` as 4.
- libstdc++'s `std::hash<std::string>` calls `_Hash_bytes`, whose algorithm is chosen by the width of `size_t` (a 32-bit Murmur variant on the device, a 64-bit one on the host), so the values differ, and the device writes at most 10 digits.

Consequences, decided: the cache-dir name must be computed on the device by the device's `std::hash`, which the shared function guarantees because the reader and the executor both call it. Pinned literal vectors were rejected: vectors from the host would not be what the device computes, and producing device vectors would need the code to run on the device or under an emulator, neither of which is available to the tests. The bookmark derivation has no platform dependency and is checked against the linked reader function.

### Decisions taken without a human (guideline rules)

- Shared cache-path function and a four-line reader refactor, rather than pinned vectors or an injected derivation: the only way the expected paths come from the reader's own code on the host while matching the device by construction (plan step 6 "Verified by"; "make the plan work").
- Bookmark derivation copied into `GitSync` and checked against the linked `BookmarkUtil`, rather than refactoring `BookmarkUtil`: the smaller change, since that function is already host-linkable.
- `prepare()` and `apply()` as separate calls, so the 8 KB compaction scratch is never alive with the 50 KiB planner.
- Cache clear for every download target, not only a modify: safe superset that matches the web server's upload behaviour and closes the stale-cache case of a path freed by an earlier delete.
- Prune inline in Delete and Move, before the journal frame: without it an interrupted run could leave an empty directory forever, and the sweep's "same files" check would fail.
- A hook returning false is fatal, not a per-file error: the journal frame must never follow a half-done migration.
- `manifest.new` named outside `*.tmp`, and its promotion in `prepare()`, to cover the remove-then-rename window the plan names.

### Verification

- **Host**: the new suite and every existing suite pass under the `ctest` command above.
- **Firmware**: `pio run` stays green with no new warning (chunk 1 changes reader code; nothing in `src/` uses the executor yet). `./bin/clang-format-fix -g` leaves no diff.
- **Step notes**: record `sizeof(SyncExecutor)`, the compaction scratch size, the bytes written by the first sync and the no-change run at the cap, and any hypothesis below that turned out false and how it was adapted.
- **Device** (owner, step 12): moving the book being read keeps its progress and bookmarks; Back mid-sync then Update resumes; Back during adoption then Update does not re-hash; free heap stays at or above 50 KB during a sync (`LOG_DBG` of `ESP.getFreeHeap()`); the summary's counts match what changed.

### Hypotheses

Step 5 (sync planner) is partly committed: its chunks 1 (path folding, `SyncPathFold.h`) and 2 (planner core, `SyncPlanner.h` and `test/git_sync_planner/`) are at HEAD; chunk 3 (moves, case collisions, unsafe-name skips, swap) is still being implemented. Each hypothesis below is marked **confirmed** (by the committed header or a committed test, cited) or **open** (rests on the step 5 PRD, `docs/prd/triage/phase1-step5-sync-planner.md`, for chunk 3). The implementer re-checks the open ones against the committed chunk 3 before executor chunk 4, adapts names, and keeps the outcomes above. Executor chunks 1 to 4 depend only on the confirmed types, so they can start now.

- **H-5-1 — confirmed** (`SyncPlanner.h`): `SyncPlanner` is allocated by the caller with `makeUniqueNoThrow` ("About 49 KB ... never on the stack"). `SyncPlanResult plan(RecordSource& manifest, RecordSource& tree, SyncLocalExistsFn localExists, void* existsCtx, const SyncOpSink& sink)`, both sources at their first byte; `SyncLocalExistsFn` is `bool (*)(void* ctx, const char* path, size_t pathLen)` with a repository-relative, `'\0'`-terminated path, answered case-insensitively. The sink is `SyncOpSink { void* ctx; bool (*onOp)(void*, const SyncOp&); void (*onTotal)(void*, uint32_t); }`.
- **H-5-2 — confirmed** (`SyncPlanner.h`): `SyncOp { kind, reason, sourceExists, targetExists, manifestOffset, treeOffset }` with `SyncOpKind { Move, AdoptOrDownload, Download, Delete, Prune, Skip }` and `SyncSkipReason { None, CaseCollision, UnsafeName }`; offsets are entry-frame offsets or `NO_OFFSET`, with the per-kind meaning used above. AdoptOrDownload's fields are not commented in the header; the executor reads its `treeOffset` (consistent with the committed test `AddedFileAlreadyOnCardIsAdoptedOrDownloaded`).
- **H-5-3 — confirmed** (`SyncPlanner.h` ordering comment; tests `OperationsComeInTheDocumentedOrder`, `SinkRefusingItsKthCallAbortsAfterExactlyKCalls`): order Move, AdoptOrDownload, Download, Delete, Prune, Skip; `onOp` returning false gives `Aborted`.
- **H-5-4 — confirmed** (`SyncPlanner.h`: "every read happens before the first operation is emitted"; test `SourceFailingAtAnyCallStopsThePlanBeforeAnyOperation`).
- **H-5-5 — open** (chunk 3): a `Move` is emitted only with `sourceExists && !targetExists` or `!sourceExists && targetExists`; both present becomes `Delete` + `AdoptOrDownload`, both missing `Delete` + `Download`. The `SyncOp` fields exist (confirmed); the existence table is not committed.
- **H-5-6 — partly confirmed**: a remote path not in the manifest is `AdoptOrDownload` when it exists locally and `Download` with no replaced entry otherwise (tests `AddedFileAlreadyOnCardIsAdoptedOrDownloaded`, `AddedFileMissingFromCardIsDownloaded`); a modify is a `Download` replacing its entry (`ModifiedFileIsDownloadedReplacingItsEntry`); a case-only rename with the same SHA emits nothing, with a new SHA it is a modify (`CaseOnlyRename...` tests). **Open** (chunk 3): a swap of two names is two modify `Download`s.
- **H-5-7 — partly confirmed**: non-syncable paths are ignored on both sides and counted (`HiddenRemoteEntriesAreIgnoredAndCounted`, `UnsyncableManifestEntriesAreNeverRemoved`). **Open** (chunk 3): `Skip` with `CaseCollision` or `UnsafeName` is emitted, and only non-skipped remote paths ever reach a `Download`, `AdoptOrDownload` or `Move` destination.
- **H-5-8 — partly confirmed**: a nested `Delete` is followed by a `Prune` naming the same manifest entry, a root `Delete` is not (`DeletedNestedFileIsDeletedThenPruned`, `DeletedRootFileIsDeletedWithoutPrune`). **Open** (chunk 3): a `Prune` for each cross-folder `Move` source, none for a move within one folder.
- **H-5-9 — confirmed** (`SyncPlanner.h`; `...OverTheCapIsRefusedWithBothCounts` tests): `MAX_SYNC_FILES = 2000` is public; a manifest or tree above it gives `CapExceeded` with both counts and no operation.
- **H-5-10 — confirmed** (`SyncPlanner.h`): `static_assert(sizeof(SyncPlanner) <= 51200)`.
- **H-5-11 — confirmed** (`SyncPlanner.h`): `SyncPlanStatus { Ok, ManifestError, TreeError, ManifestNotCompacted, CapExceeded, Aborted }`, `readerStatus`, per-kind counts (`moves`, `adoptOrDownloads`, `downloads`, `deletes`, `prunes`), `caseCollisionSkips`, `unsafeNameSkips`, `hiddenIgnored`, `malformedIgnored`, `manifestEntries`, `treeEntries`. The executor's mapping is a decision of this PRD: `ManifestNotCompacted` → `ManifestError`; `Aborted` → `Cancelled` when the cancel flag is set, otherwise `StorageError` (the sink refused after a fatal outcome).
- **H-5-12 — confirmed, and the earlier assumption was wrong** (`SyncPlanner.h`; tests `TotalIsReportedOnceBeforeTheFirstOperation`, `EmptyPlanReportsATotalOfZero`, `TotalCallbackIsOptional`, `FailedPlanReportsNoTotal`): `onTotal` is called exactly once with the number of operations before the first `onOp`, 0 included, and never when the plan fails. The executor always supplies `onTotal` and reports `n/total` with it (Injected callbacks, "Progress").
- **H-5-13 — confirmed** (`SyncPathFold.h`): `syncPathsEqualFolded(a, aLen, b, bLen)`, `syncPathKey(path, len)` and `isFatSafeName(path, len)` are public and host-compiled by the planner suite; the in-memory storage uses the first for lookup and the third for name rejection.
- **H-5-14 — confirmed** (`SyncPlanner.h`: "Plans a sync from a compacted manifest"; test `ManifestWithAJournalIsRefusedAsNotCompacted`).
